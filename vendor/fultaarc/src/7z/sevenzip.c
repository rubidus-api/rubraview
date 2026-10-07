/* sevenzip.c - FultaArc: 7z reader. MIT. Written from docs/specs/7z.md (FultaArc's format document, from the LZMA
 * SDK's public-domain DOC/7zFormat.txt and 7zArcIn.c) and docs/specs/crypto.md section 3 (7zAES). Codecs: the
 * vendored LZMA SDK (LZMA, LZMA2, PPMd, branch filters, Delta), bzip2 1.0.8, FultaArc's inflate and BCJ2. */
#include "../core/internal.h"

#include <stdio.h>

#define MAX_CODERS 64
#define MAX_HEADER (256u << 20)

typedef struct sz_coder {
    uint8_t id[8];
    uint8_t idlen;
    uint32_t nin;
    uint8_t *props;
    uint32_t nprops;
} sz_coder_t;

typedef struct sz_folder {
    sz_coder_t *coders;
    uint32_t ncoders, nin;
    uint32_t (*binds)[2];             /* (in index, out coder) */
    uint32_t nbinds;
    uint32_t *packed;                 /* in index of each packed stream */
    uint32_t npacked;
    uint64_t *sizes;                  /* unpack size per coder */
    uint32_t main;
    bool has_crc;
    uint32_t crc;
    uint32_t first_pack;              /* index into pack streams */
    bool encrypted;
} sz_folder_t;

typedef struct sz_streams {
    uint64_t pack_pos;
    uint64_t *pack_sizes;
    uint32_t npack;
    sz_folder_t *folders;
    uint32_t nfolders;
    uint32_t *nsub;                   /* substreams per folder */
    uint64_t *sub_sizes;              /* flattened */
    bool *sub_has_crc;
    uint32_t *sub_crc;
    uint64_t nsubtotal;
} sz_streams_t;

typedef struct sz_file { uint32_t folder; uint64_t offset, size; bool has_crc; uint32_t crc; } sz_file_t;

typedef struct sz_state {
    uint64_t base;                    /* offset of the signature in the source (SFX) */
    fa_range_t range;                 /* all .7z.NNN parts joined */
    sz_streams_t main;
    sz_file_t *files;                 /* per entry; folder == UINT32_MAX for no data */
    /* sequential solid extraction */
    fa_stream_t *cur;
    uint32_t cur_folder;
    uint64_t cur_pos;
    /* 7zAES key cache */
    uint8_t key_salt[16], key_props_hash[32], key[32];
    bool key_valid;
    char *key_pw;
} sz_state_t;

static const uint8_t SIG[6] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};

static bool sz_probe(const uint8_t *h, size_t n, uint64_t *off) {
    *off = 0;
    return n >= 6 && memcmp(h, SIG, 6) == 0;
}

/* ---- header reading ------------------------------------------------------------------------------------------ */

typedef struct rd { const uint8_t *p; size_t n, pos; bool bad; } rd_t;

static uint8_t rd_byte(rd_t *r) {
    if (r->pos >= r->n) { r->bad = true; return 0; }
    return r->p[r->pos++];
}

static uint64_t rd_num(rd_t *r) {
    uint8_t first = rd_byte(r);
    int n = 0;
    uint8_t mask = 0x80;
    while (n < 8 && (first & mask)) { n++; mask >>= 1; }
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint64_t)rd_byte(r) << (8 * i);
    if (n < 8) v += (uint64_t)(first & (mask - 1)) << (8 * n);
    return v;
}

static uint32_t rd_u32(rd_t *r) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)rd_byte(r) << (8 * i);
    return v;
}

static uint64_t rd_u64(rd_t *r) { uint64_t lo = rd_u32(r); return lo | ((uint64_t)rd_u32(r) << 32); }

static bool *rd_bits(rd_t *r, uint64_t n) {
    bool *v = fa_calloc(n ? (size_t)n : 1, sizeof *v);
    if (!v) { r->bad = true; return NULL; }
    uint8_t b = 0;
    for (uint64_t i = 0; i < n; i++) {
        if ((i & 7) == 0) b = rd_byte(r);
        v[i] = (b >> (7 - (i & 7))) & 1;
    }
    return v;
}

static bool *rd_defined(rd_t *r, uint64_t n) {
    uint8_t all = rd_byte(r);
    if (!all) return rd_bits(r, n);
    bool *v = fa_calloc(n ? (size_t)n : 1, sizeof *v);
    if (!v) { r->bad = true; return NULL; }
    for (uint64_t i = 0; i < n; i++) v[i] = true;
    return v;
}

static void rd_skip(rd_t *r, uint64_t n) {
    if (n > r->n - r->pos) { r->bad = true; r->pos = r->n; return; }
    r->pos += (size_t)n;
}

static void free_streams(sz_streams_t *s) {
    for (uint32_t i = 0; i < s->nfolders; i++) {
        sz_folder_t *f = &s->folders[i];
        for (uint32_t c = 0; c < f->ncoders; c++) fa_free(f->coders[c].props);
        fa_free(f->coders);
        fa_free(f->binds);
        fa_free(f->packed);
        fa_free(f->sizes);
    }
    fa_free(s->folders);
    fa_free(s->pack_sizes);
    fa_free(s->nsub);
    fa_free(s->sub_sizes);
    fa_free(s->sub_has_crc);
    fa_free(s->sub_crc);
    memset(s, 0, sizeof *s);
}

static bool coder_is(const sz_coder_t *c, uint32_t id) {
    uint32_t v = 0;
    for (int i = 0; i < c->idlen; i++) v = (v << 8) | c->id[i];
    return c->idlen && c->idlen <= 4 && v == id;
}

static fulta_arc_err_t parse_folder(rd_t *r, sz_folder_t *f) {
    uint64_t nc = rd_num(r);
    if (r->bad || nc == 0 || nc > MAX_CODERS) return FULTA_ARC_ERR_UNSUPPORTED;
    f->coders = fa_calloc((size_t)nc, sizeof *f->coders);
    if (!f->coders) return FULTA_ARC_ERR_NOMEM;
    f->ncoders = (uint32_t)nc;
    for (uint32_t i = 0; i < f->ncoders; i++) {
        sz_coder_t *c = &f->coders[i];
        uint8_t fl = rd_byte(r);
        if (fl & 0xC0) return FULTA_ARC_ERR_UNSUPPORTED;
        c->idlen = fl & 0x0F;
        if (c->idlen > 8) return FULTA_ARC_ERR_UNSUPPORTED;
        for (int k = 0; k < c->idlen; k++) c->id[k] = rd_byte(r);
        c->nin = 1;
        if (fl & 0x10) {
            uint64_t nin = rd_num(r), nout = rd_num(r);
            if (nout != 1 || nin == 0 || nin > MAX_CODERS) return FULTA_ARC_ERR_UNSUPPORTED;
            c->nin = (uint32_t)nin;
        }
        if (fl & 0x20) {
            uint64_t np = rd_num(r);
            if (np > r->n - r->pos) return FULTA_ARC_ERR_CORRUPT;
            c->props = fa_malloc((size_t)np);
            if (!c->props) return FULTA_ARC_ERR_NOMEM;
            memcpy(c->props, r->p + r->pos, (size_t)np);
            c->nprops = (uint32_t)np;
            r->pos += (size_t)np;
        }
        f->nin += c->nin;
        if (f->nin > MAX_CODERS) return FULTA_ARC_ERR_UNSUPPORTED;
        if (coder_is(c, 0x06F10701)) f->encrypted = true;
    }
    f->nbinds = f->ncoders - 1;
    f->binds = fa_calloc(f->nbinds ? f->nbinds : 1, sizeof *f->binds);
    if (!f->binds) return FULTA_ARC_ERR_NOMEM;
    for (uint32_t i = 0; i < f->nbinds; i++) {
        uint64_t in = rd_num(r), out = rd_num(r);
        if (in >= f->nin || out >= f->ncoders) return FULTA_ARC_ERR_CORRUPT;
        for (uint32_t k = 0; k < i; k++)
            if (f->binds[k][0] == in || f->binds[k][1] == out) return FULTA_ARC_ERR_CORRUPT;
        f->binds[i][0] = (uint32_t)in;
        f->binds[i][1] = (uint32_t)out;
    }
    if (f->nin < f->nbinds) return FULTA_ARC_ERR_CORRUPT;
    f->npacked = f->nin - f->nbinds;
    f->packed = fa_calloc(f->npacked ? f->npacked : 1, sizeof *f->packed);
    if (!f->packed) return FULTA_ARC_ERR_NOMEM;
    if (f->npacked == 1) {
        for (uint32_t i = 0; i < f->nin; i++) {
            bool bound = false;
            for (uint32_t k = 0; k < f->nbinds; k++) bound |= f->binds[k][0] == i;
            if (!bound) { f->packed[0] = i; break; }
        }
    } else {
        for (uint32_t i = 0; i < f->npacked; i++) {
            uint64_t v = rd_num(r);
            if (v >= f->nin) return FULTA_ARC_ERR_CORRUPT;
            f->packed[i] = (uint32_t)v;
        }
    }
    /* the main coder: its output is bound nowhere */
    f->main = UINT32_MAX;
    for (uint32_t c = 0; c < f->ncoders; c++) {
        bool used = false;
        for (uint32_t k = 0; k < f->nbinds; k++) used |= f->binds[k][1] == c;
        if (!used) {
            if (f->main != UINT32_MAX) return FULTA_ARC_ERR_CORRUPT;
            f->main = c;
        }
    }
    return f->main == UINT32_MAX ? FULTA_ARC_ERR_CORRUPT : FULTA_ARC_OK;
}

static fulta_arc_err_t parse_streams(rd_t *r, sz_streams_t *s) {
    fulta_arc_err_t e;
    uint64_t t = rd_num(r);
    if (t == 0x06) {
        s->pack_pos = rd_num(r);
        uint64_t np = rd_num(r);
        if (r->bad || np > (1u << 24)) return FULTA_ARC_ERR_CORRUPT;
        s->npack = (uint32_t)np;
        s->pack_sizes = fa_calloc(np ? (size_t)np : 1, sizeof *s->pack_sizes);
        if (!s->pack_sizes) return FULTA_ARC_ERR_NOMEM;
        for (;;) {
            uint64_t t2 = rd_num(r);
            if (r->bad) return FULTA_ARC_ERR_CORRUPT;
            if (t2 == 0) break;
            if (t2 == 0x09) {
                for (uint32_t i = 0; i < s->npack; i++) s->pack_sizes[i] = rd_num(r);
            } else if (t2 == 0x0A) {
                bool *d = rd_defined(r, np);
                if (!d) return FULTA_ARC_ERR_NOMEM;
                for (uint32_t i = 0; i < s->npack; i++) if (d[i]) rd_u32(r);
                fa_free(d);
            } else {
                rd_skip(r, rd_num(r));
            }
        }
        t = rd_num(r);
    }
    if (t == 0x07) {
        if (rd_num(r) != 0x0B) return FULTA_ARC_ERR_CORRUPT;
        uint64_t nf = rd_num(r);
        if (r->bad || nf > (1u << 24)) return FULTA_ARC_ERR_CORRUPT;
        if (rd_byte(r) != 0) return FULTA_ARC_ERR_UNSUPPORTED;    /* external folders (additional streams) */
        s->folders = fa_calloc(nf ? (size_t)nf : 1, sizeof *s->folders);
        if (!s->folders) return FULTA_ARC_ERR_NOMEM;
        s->nfolders = (uint32_t)nf;
        uint32_t pack_index = 0;
        for (uint32_t i = 0; i < s->nfolders; i++) {
            if ((e = parse_folder(r, &s->folders[i]))) return e;
            s->folders[i].first_pack = pack_index;
            pack_index += s->folders[i].npacked;
            if (pack_index > s->npack) return FULTA_ARC_ERR_CORRUPT;
        }
        if (rd_num(r) != 0x0C) return FULTA_ARC_ERR_CORRUPT;
        for (uint32_t i = 0; i < s->nfolders; i++) {
            sz_folder_t *f = &s->folders[i];
            f->sizes = fa_calloc(f->ncoders, sizeof *f->sizes);
            if (!f->sizes) return FULTA_ARC_ERR_NOMEM;
            for (uint32_t c = 0; c < f->ncoders; c++) f->sizes[c] = rd_num(r);
        }
        for (;;) {
            uint64_t t2 = rd_num(r);
            if (r->bad) return FULTA_ARC_ERR_CORRUPT;
            if (t2 == 0) break;
            if (t2 == 0x0A) {
                bool *d = rd_defined(r, nf);
                if (!d) return FULTA_ARC_ERR_NOMEM;
                for (uint32_t i = 0; i < s->nfolders; i++)
                    if (d[i]) { s->folders[i].has_crc = true; s->folders[i].crc = rd_u32(r); }
                fa_free(d);
            } else {
                rd_skip(r, rd_num(r));
            }
        }
        t = rd_num(r);
    }
    /* substreams: default one per folder */
    s->nsub = fa_calloc(s->nfolders ? s->nfolders : 1, sizeof *s->nsub);
    if (!s->nsub) return FULTA_ARC_ERR_NOMEM;
    for (uint32_t i = 0; i < s->nfolders; i++) s->nsub[i] = 1;
    bool sizes_read = false;
    if (t == 0x08) {
        uint64_t t2 = rd_num(r);
        if (t2 == 0x0D) {
            for (uint32_t i = 0; i < s->nfolders; i++) {
                uint64_t v = rd_num(r);
                if (v > (1u << 24)) return FULTA_ARC_ERR_CORRUPT;
                s->nsub[i] = (uint32_t)v;
            }
            t2 = rd_num(r);
        }
        for (uint32_t i = 0; i < s->nfolders; i++) s->nsubtotal += s->nsub[i];
        if (s->nsubtotal > (1u << 24)) return FULTA_ARC_ERR_CORRUPT;
        s->sub_sizes = fa_calloc(s->nsubtotal ? (size_t)s->nsubtotal : 1, sizeof *s->sub_sizes);
        s->sub_has_crc = fa_calloc(s->nsubtotal ? (size_t)s->nsubtotal : 1, sizeof *s->sub_has_crc);
        s->sub_crc = fa_calloc(s->nsubtotal ? (size_t)s->nsubtotal : 1, sizeof *s->sub_crc);
        if (!s->sub_sizes || !s->sub_has_crc || !s->sub_crc) return FULTA_ARC_ERR_NOMEM;
        size_t k = 0;
        for (uint32_t i = 0; i < s->nfolders; i++) {
            uint64_t fsize = s->folders[i].sizes[s->folders[i].main], sum = 0;
            for (uint32_t j = 0; j < s->nsub[i]; j++, k++) {
                if (j + 1 < s->nsub[i]) {
                    uint64_t v = t2 == 0x09 ? rd_num(r) : 0;
                    if (t2 != 0x09) return FULTA_ARC_ERR_CORRUPT;
                    s->sub_sizes[k] = v;
                    sum += v;
                } else {
                    if (sum > fsize) return FULTA_ARC_ERR_CORRUPT;
                    s->sub_sizes[k] = fsize - sum;
                }
            }
        }
        sizes_read = true;
        if (t2 == 0x09) t2 = rd_num(r);
        /* CRCs of the substreams whose CRC is not the folder's */
        k = 0;
        uint64_t need = 0;
        for (uint32_t i = 0; i < s->nfolders; i++)
            need += (s->nsub[i] == 1 && s->folders[i].has_crc) ? 0 : s->nsub[i];
        while (t2 != 0) {
            if (r->bad) return FULTA_ARC_ERR_CORRUPT;
            if (t2 == 0x0A) {
                bool *d = rd_defined(r, need);
                if (!d) return FULTA_ARC_ERR_NOMEM;
                size_t di = 0, si = 0;
                for (uint32_t i = 0; i < s->nfolders; i++) {
                    if (s->nsub[i] == 1 && s->folders[i].has_crc) { si++; continue; }
                    for (uint32_t j = 0; j < s->nsub[i]; j++, si++, di++)
                        if (d[di]) { s->sub_has_crc[si] = true; s->sub_crc[si] = rd_u32(r); }
                }
                fa_free(d);
            } else {
                rd_skip(r, rd_num(r));
            }
            t2 = rd_num(r);
        }
        t = rd_num(r);
    }
    if (!sizes_read) {
        s->nsubtotal = s->nfolders;
        s->sub_sizes = fa_calloc(s->nfolders ? s->nfolders : 1, sizeof *s->sub_sizes);
        s->sub_has_crc = fa_calloc(s->nfolders ? s->nfolders : 1, sizeof *s->sub_has_crc);
        s->sub_crc = fa_calloc(s->nfolders ? s->nfolders : 1, sizeof *s->sub_crc);
        if (!s->sub_sizes || !s->sub_has_crc || !s->sub_crc) return FULTA_ARC_ERR_NOMEM;
    }
    for (uint32_t i = 0; i < s->nfolders && !sizes_read; i++) s->sub_sizes[i] = s->folders[i].sizes[s->folders[i].main];
    /* a folder with one substream and a folder CRC: the substream takes the folder's CRC */
    size_t k = 0;
    for (uint32_t i = 0; i < s->nfolders; i++) {
        if (s->nsub[i] == 1 && s->folders[i].has_crc && !s->sub_has_crc[k]) {
            s->sub_has_crc[k] = true;
            s->sub_crc[k] = s->folders[i].crc;
        }
        k += s->nsub[i];
    }
    if (t != 0 || r->bad) return FULTA_ARC_ERR_CORRUPT;
    return FULTA_ARC_OK;
}

/* ---- decoding a folder --------------------------------------------------------------------------------------- */

static fulta_arc_err_t get_key(fulta_arc_t *arc, const sz_coder_t *c, uint32_t attempt, uint8_t key[32], uint8_t iv[16]) {
    sz_state_t *st = arc->state;
    if (c->nprops < 1) return FULTA_ARC_ERR_CORRUPT;
    uint8_t b0 = c->props[0];
    uint32_t power = b0 & 0x3F;
    uint32_t salt_len = 0, iv_len = 0;
    const uint8_t *salt = NULL, *ivp = NULL;
    if (b0 & 0xC0) {
        if (c->nprops < 2) return FULTA_ARC_ERR_CORRUPT;
        uint8_t b1 = c->props[1];
        salt_len = ((b0 >> 7) & 1) + (b1 >> 4);
        iv_len = ((b0 >> 6) & 1) + (b1 & 0x0F);
        if (c->nprops != 2 + salt_len + iv_len) return FULTA_ARC_ERR_CORRUPT;
        salt = c->props + 2;
        ivp = c->props + 2 + salt_len;
    } else if (c->nprops != 1) {
        return FULTA_ARC_ERR_CORRUPT;
    }
    memset(iv, 0, 16);
    if (iv_len) memcpy(iv, ivp, iv_len);
    if (power != 0x3F && power > 24) return FULTA_ARC_ERR_LIMIT;
    const char *pw;
    fulta_arc_err_t e = fa_password(arc, attempt, &pw);
    if (e) return e;
    if (!st->key_pw || strcmp(st->key_pw, pw) != 0) {      /* remember which password this key came from */
        char *c = fa_strndup(pw, strlen(pw));
        if (!c) return FULTA_ARC_ERR_NOMEM;
        fa_free(st->key_pw);
        st->key_pw = c;
    }
    /* cache by (password, properties) */
    fa_sha256_t h;
    uint8_t ph[32];
    fa_sha256_init(&h);
    fa_sha256_update(&h, c->props, c->nprops);
    fa_sha256_update(&h, pw, strlen(pw));
    fa_sha256_final(&h, ph);
    if (st->key_valid && memcmp(ph, st->key_props_hash, 32) == 0) { memcpy(key, st->key, 32); return FULTA_ARC_OK; }
    /* password as UTF-16LE */
    fa_buf_t u = {0};
    const uint8_t *s = (const uint8_t *)pw;
    size_t n = strlen(pw);
    for (size_t i = 0; i < n;) {
        uint32_t cp = s[i];
        size_t len = 1;
        if (cp >= 0xF0 && i + 3 < n) { cp = ((cp & 7) << 18) | ((s[i + 1] & 63) << 12) | ((s[i + 2] & 63) << 6) | (s[i + 3] & 63); len = 4; }
        else if (cp >= 0xE0 && i + 2 < n) { cp = ((cp & 15) << 12) | ((s[i + 1] & 63) << 6) | (s[i + 2] & 63); len = 3; }
        else if (cp >= 0xC0 && i + 1 < n) { cp = ((cp & 31) << 6) | (s[i + 1] & 63); len = 2; }
        i += len;
        uint8_t t[4];
        if (cp >= 0x10000) {
            cp -= 0x10000;
            fa_put_le16(t, (uint16_t)(0xD800 + (cp >> 10)));
            fa_put_le16(t + 2, (uint16_t)(0xDC00 + (cp & 0x3FF)));
            e = fa_buf_append(&u, t, 4);
        } else {
            fa_put_le16(t, (uint16_t)cp);
            e = fa_buf_append(&u, t, 2);
        }
        if (e) { fa_buf_free(&u); return e; }
    }
    if (power == 0x3F) {
        memset(key, 0, 32);
        size_t k = 0;
        for (uint32_t i = 0; i < salt_len && k < 32; i++) key[k++] = salt[i];
        for (size_t i = 0; i < u.size && k < 32; i++) key[k++] = u.data[i];
    } else {
        fa_sha256_init(&h);
        uint8_t *blk = fa_malloc(salt_len + u.size + 8);
        if (!blk) { fa_buf_free(&u); return FULTA_ARC_ERR_NOMEM; }
        if (salt_len) memcpy(blk, salt, salt_len);
        if (u.size) memcpy(blk + salt_len, u.data, u.size);
        size_t bl = salt_len + u.size;
        for (uint64_t i = 0; i < (UINT64_C(1) << power); i++) {
            fa_put_le64(blk + bl, i);
            fa_sha256_update(&h, blk, bl + 8);
        }
        fa_free(blk);
        fa_sha256_final(&h, key);
    }
    fa_buf_free(&u);
    memcpy(st->key, key, 32);
    memcpy(st->key_props_hash, ph, 32);
    st->key_valid = true;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t open_coder(fulta_arc_t *arc, const sz_streams_t *ss, uint32_t fi, uint32_t ci,
                                  fa_stream_t **packs, uint32_t attempt, int depth, fa_stream_t **out);

static fulta_arc_err_t input_of(fulta_arc_t *arc, const sz_streams_t *ss, uint32_t fi, uint32_t in_index,
                                fa_stream_t **packs, uint32_t attempt, int depth, fa_stream_t **out) {
    const sz_folder_t *f = &ss->folders[fi];
    for (uint32_t k = 0; k < f->nbinds; k++)
        if (f->binds[k][0] == in_index) return open_coder(arc, ss, fi, f->binds[k][1], packs, attempt, depth + 1, out);
    for (uint32_t k = 0; k < f->npacked; k++)
        if (f->packed[k] == in_index) {
            if (!packs[k]) return FULTA_ARC_ERR_CORRUPT;   /* a packed stream used twice */
            *out = packs[k];
            packs[k] = NULL;
            return FULTA_ARC_OK;
        }
    return FULTA_ARC_ERR_CORRUPT;
}

static fulta_arc_err_t open_coder(fulta_arc_t *arc, const sz_streams_t *ss, uint32_t fi, uint32_t ci,
                                  fa_stream_t **packs, uint32_t attempt, int depth, fa_stream_t **out) {
    const sz_folder_t *f = &ss->folders[fi];
    if (depth > MAX_CODERS) return FULTA_ARC_ERR_CORRUPT;
    const sz_coder_t *c = &f->coders[ci];
    uint32_t first_in = 0;
    for (uint32_t k = 0; k < ci; k++) first_in += f->coders[k].nin;
    uint64_t size = f->sizes[ci];
    fulta_arc_err_t e;
    if (coder_is(c, 0x0303011B)) {      /* BCJ2: four inputs */
        if (c->nin != 4) return FULTA_ARC_ERR_CORRUPT;
        fa_stream_t *ins[4] = {0};
        for (uint32_t k = 0; k < 4; k++) {
            if ((e = input_of(arc, ss, fi, first_in + k, packs, attempt, depth, &ins[k]))) {
                for (uint32_t j = 0; j < k; j++) fa_stream_destroy(ins[j]);
                return e;
            }
        }
        return fa_dec_bcj2(ins, size, out);
    }
    if (c->nin != 1) return FULTA_ARC_ERR_UNSUPPORTED;
    fa_stream_t *in;
    if ((e = input_of(arc, ss, fi, first_in, packs, attempt, depth, &in))) return e;
    const uint8_t *p = c->props;
    uint32_t np = c->nprops;
    if (coder_is(c, 0x00)) return fa_stream_limit(in, size, out);
    if (coder_is(c, 0x030101)) {
        if (np != 5) { fa_stream_destroy(in); return FULTA_ARC_ERR_UNSUPPORTED; }
        return fa_dec_lzma(in, p, size, &arc->limits, out);
    }
    if (coder_is(c, 0x21)) {
        if (np != 1) { fa_stream_destroy(in); return FULTA_ARC_ERR_UNSUPPORTED; }
        return fa_dec_lzma2(in, p[0], size, &arc->limits, out);
    }
    if (coder_is(c, 0x030401)) return fa_dec_ppmd7z(in, p, np, size, &arc->limits, out);
    if (coder_is(c, 0x040108)) return fa_dec_deflate(in, false, size, out);
    if (coder_is(c, 0x040109)) return fa_dec_deflate(in, true, size, out);
    if (coder_is(c, 0x040202)) return fa_dec_bzip2(in, size, out);
    if (coder_is(c, 0x04F71101)) return fa_dec_zstd(in, size, &arc->limits, out);   /* 7-Zip ZS's id */
    uint32_t start = np == 4 ? fa_le32(p) : 0;
    struct { uint32_t id; fa_filter_kind_t k; } F[] = {
        {0x03030103, FA_FILTER_X86}, {0x03030501, FA_FILTER_ARM}, {0x03030701, FA_FILTER_ARMT},
        {0x0A, FA_FILTER_ARM64}, {0x03030205, FA_FILTER_PPC}, {0x03030805, FA_FILTER_SPARC},
        {0x03030401, FA_FILTER_IA64}, {0x0B, FA_FILTER_RISCV}, {0x020302, FA_FILTER_SWAP2}, {0x020304, FA_FILTER_SWAP4},
    };
    for (size_t k = 0; k < sizeof F / sizeof *F; k++)
        if (coder_is(c, F[k].id)) {
            if ((e = fa_dec_filter(in, F[k].k, start, &in))) return e;
            return fa_stream_limit(in, size, out);
        }
    if (coder_is(c, 0x03)) {
        if (np != 1) { fa_stream_destroy(in); return FULTA_ARC_ERR_UNSUPPORTED; }
        if ((e = fa_dec_filter(in, FA_FILTER_DELTA, (uint32_t)p[0] + 1, &in))) return e;
        return fa_stream_limit(in, size, out);
    }
    if (coder_is(c, 0x06F10701)) {
        uint8_t key[32], iv[16];
        if ((e = get_key(arc, c, attempt, key, iv))) { fa_stream_destroy(in); return e; }
        if ((e = fa_dec_aes_cbc(in, key, 32, iv, &in))) return e;
        memset(key, 0, sizeof key);
        return fa_stream_limit(in, size, out);
    }
    fa_stream_destroy(in);
    return FULTA_ARC_ERR_UNSUPPORTED;
}

static fulta_arc_err_t open_folder(fulta_arc_t *arc, const sz_streams_t *ss, uint32_t fi, uint32_t attempt,
                                   fa_stream_t **out) {
    sz_state_t *st = arc->state;
    const sz_folder_t *f = &ss->folders[fi];
    uint64_t pos = st->base + 32 + ss->pack_pos;
    for (uint32_t k = 0; k < f->first_pack; k++) pos += ss->pack_sizes[k];
    fa_stream_t *packs[MAX_CODERS] = {0};
    fulta_arc_err_t e = FULTA_ARC_OK;
    for (uint32_t k = 0; k < f->npacked && !e; k++) {
        uint64_t n = ss->pack_sizes[f->first_pack + k];
        e = fa_stream_range(&st->range, pos, n, &packs[k]);
        pos += n;
    }
    if (!e) e = open_coder(arc, ss, fi, f->main, packs, attempt, 0, out);
    for (uint32_t k = 0; k < f->npacked; k++) fa_stream_destroy(packs[k]);
    return e;
}

/* Decode folder fi of `ss` into memory (headers). */
static fulta_arc_err_t folder_to_memory(fulta_arc_t *arc, const sz_streams_t *ss, uint32_t fi, uint32_t attempt,
                                        uint8_t **out, size_t *outlen) {
    const sz_folder_t *f = &ss->folders[fi];
    uint64_t size = f->sizes[f->main];
    if (size > MAX_HEADER) return FULTA_ARC_ERR_LIMIT;
    fa_stream_t *s;
    fulta_arc_err_t e = open_folder(arc, ss, fi, attempt, &s);
    if (e) return e;
    uint8_t *buf = fa_malloc((size_t)size);
    if (!buf) { fa_stream_destroy(s); return FULTA_ARC_ERR_NOMEM; }
    e = fa_stream_read_exact(s, buf, (size_t)size);
    fa_stream_destroy(s);
    if (!e && f->has_crc && fa_crc32(0, buf, (size_t)size) != f->crc) e = FULTA_ARC_ERR_CHECKSUM;
    if (e) { fa_free(buf); return e; }
    *out = buf;
    *outlen = (size_t)size;
    return FULTA_ARC_OK;
}

/* ---- opening --------------------------------------------------------------------------------------------- */

static fulta_arc_err_t parse_files(fulta_arc_t *arc, rd_t *r) {
    sz_state_t *st = arc->state;
    sz_streams_t *ss = &st->main;
    uint64_t nfiles = rd_num(r);
    if (r->bad || nfiles > (1u << 24)) return FULTA_ARC_ERR_CORRUPT;
    bool *empty = NULL, *emptyfile = NULL, *anti = NULL, *mdef = NULL, *adef = NULL;
    uint64_t *mt = NULL;
    uint32_t *at = NULL;
    const uint8_t *names = NULL;
    size_t names_len = 0;
    uint64_t nempty = 0;
    fulta_arc_err_t e = FULTA_ARC_OK;
    for (;;) {
        uint64_t t = rd_num(r);
        if (r->bad) { e = FULTA_ARC_ERR_CORRUPT; goto done; }
        if (t == 0) break;
        uint64_t size = rd_num(r);
        if (r->bad || size > r->n - r->pos) { e = FULTA_ARC_ERR_CORRUPT; goto done; }
        rd_t sub = {r->p + r->pos, (size_t)size, 0, false};
        r->pos += (size_t)size;
        if (t == 0x0E) {
            fa_free(empty);
            empty = rd_bits(&sub, nfiles);
            nempty = 0;
            for (uint64_t i = 0; empty && i < nfiles; i++) nempty += empty[i];
        } else if (t == 0x0F) {
            fa_free(emptyfile);
            emptyfile = rd_bits(&sub, nempty);
        } else if (t == 0x10) {
            fa_free(anti);
            anti = rd_bits(&sub, nempty);
        } else if (t == 0x11) {
            if (rd_byte(&sub) != 0) { e = FULTA_ARC_ERR_UNSUPPORTED; goto done; }
            names = sub.p + 1;
            names_len = sub.n - 1;
        } else if (t == 0x14 || t == 0x15) {
            bool *d = rd_defined(&sub, nfiles);
            if (!d) { e = FULTA_ARC_ERR_NOMEM; goto done; }
            if (rd_byte(&sub) != 0) { fa_free(d); e = FULTA_ARC_ERR_UNSUPPORTED; goto done; }
            if (t == 0x14) {
                mt = fa_calloc(nfiles ? (size_t)nfiles : 1, sizeof *mt);
                for (uint64_t i = 0; mt && i < nfiles; i++) if (d[i]) mt[i] = rd_u64(&sub);
                fa_free(mdef);
                mdef = d;
            } else {
                at = fa_calloc(nfiles ? (size_t)nfiles : 1, sizeof *at);
                for (uint64_t i = 0; at && i < nfiles; i++) if (d[i]) at[i] = rd_u32(&sub);
                fa_free(adef);
                adef = d;
            }
            if ((t == 0x14 && !mt) || (t == 0x15 && !at)) { e = FULTA_ARC_ERR_NOMEM; goto done; }
        }
        if (sub.bad) { e = FULTA_ARC_ERR_CORRUPT; goto done; }
    }
    if (nfiles - nempty != ss->nsubtotal) { e = FULTA_ARC_ERR_CORRUPT; goto done; }
    st->files = fa_calloc(nfiles ? (size_t)nfiles : 1, sizeof *st->files);
    if (!st->files) { e = FULTA_ARC_ERR_NOMEM; goto done; }
    /* map non-empty files to substreams in order */
    uint32_t fi = 0, sj = 0;
    uint64_t sub_index = 0, off = 0;
    size_t np = 0;
    uint64_t ei = 0;
    for (uint64_t i = 0; i < nfiles; i++) {
        /* name */
        size_t q = np;
        bool found = false;
        while (names && q + 1 < names_len) {
            if (!names[q] && !names[q + 1]) { found = true; break; }
            q += 2;
        }
        if (names && !found) { e = FULTA_ARC_ERR_CORRUPT; goto done; }
        char *uname = names ? fa_utf16le_to_utf8(names + np, (q - np) / 2) : fa_strndup("", 0);
        const uint8_t *raw = names ? names + np : (const uint8_t *)"";
        size_t rawlen = names ? q - np : 0;
        if (names) np = q + 2;
        if (!uname) { e = FULTA_ARC_ERR_NOMEM; goto done; }
        bool is_empty = empty && empty[i];
        bool is_dir = false, is_anti = false;
        if (is_empty) {
            is_dir = !(emptyfile && emptyfile[ei]);
            is_anti = anti && anti[ei];
            ei++;
        }
        sz_file_t sf = {.folder = UINT32_MAX};
        char method[96] = "";
        if (!is_empty) {
            while (fi < ss->nfolders && sj >= ss->nsub[fi]) { fi++; sj = 0; off = 0; }
            if (fi >= ss->nfolders) { fa_free(uname); e = FULTA_ARC_ERR_CORRUPT; goto done; }
            sf.folder = fi;
            sf.offset = off;
            sf.size = ss->sub_sizes[sub_index];
            sf.has_crc = ss->sub_has_crc[sub_index];
            sf.crc = ss->sub_crc[sub_index];
            off += sf.size;
            sj++;
            sub_index++;
            const sz_folder_t *f = &ss->folders[fi];
            for (uint32_t c = 0; c < f->ncoders; c++) {
                const sz_coder_t *cd = &f->coders[f->ncoders - 1 - c];
                const char *nm = coder_is(cd, 0x21) ? "LZMA2" : coder_is(cd, 0x030101) ? "LZMA" : coder_is(cd, 0x030401) ? "PPMd"
                               : coder_is(cd, 0x040202) ? "BZip2" : coder_is(cd, 0x04F71101) ? "ZSTD" : coder_is(cd, 0x040108) ? "Deflate" : coder_is(cd, 0x040109) ? "Deflate64"
                               : coder_is(cd, 0x00) ? "Copy" : coder_is(cd, 0x06F10701) ? "7zAES" : coder_is(cd, 0x03030103) ? "BCJ"
                               : coder_is(cd, 0x0303011B) ? "BCJ2" : coder_is(cd, 0x03) ? "Delta" : "Filter";
                size_t L = strlen(method);
                snprintf(method + L, sizeof method - L, "%s%s", L ? ":" : "", nm);
            }
        }
        fa_entry_t *en = fa_add_entry(arc, raw, rawlen, uname, method);
        fa_free(uname);
        if (!en) { e = FULTA_ARC_ERR_NOMEM; goto done; }
        en->pub.size = sf.size;
        if (is_dir) en->pub.flags |= FULTA_ARC_ENTRY_DIR;
        if (sf.has_crc) { en->pub.flags |= FULTA_ARC_ENTRY_HAS_CRC32; en->pub.crc32 = sf.crc; }
        if (mt && mdef[i]) { en->pub.mtime = fa_filetime_ns(mt[i]); en->pub.flags |= FULTA_ARC_ENTRY_HAS_MTIME; }
        if (at && adef[i]) {
            en->pub.attributes = at[i];
            if (at[i] & 0x10) en->pub.flags |= FULTA_ARC_ENTRY_DIR;
            if (at[i] & 0x8000) {
                en->pub.unix_mode = at[i] >> 16;
                if ((en->pub.unix_mode & 0xF000) == 0xA000) en->pub.flags |= FULTA_ARC_ENTRY_SYMLINK;
            }
        }
        if (sf.folder != UINT32_MAX) {
            const sz_folder_t *f = &ss->folders[sf.folder];
            if (f->encrypted) en->pub.flags |= FULTA_ARC_ENTRY_ENCRYPTED;
            if (ss->nsub[sf.folder] > 1) en->pub.flags |= FULTA_ARC_ENTRY_SOLID;
        }
        if (is_anti) en->pub.flags |= FULTA_ARC_ENTRY_UNSUPPORTED;
        st->files[arc->count - 1] = sf;
    }
done:
    fa_free(empty); fa_free(emptyfile); fa_free(anti); fa_free(mdef); fa_free(adef); fa_free(mt); fa_free(at);
    return e;
}

static fulta_arc_err_t sz_open(fulta_arc_t *arc, uint64_t search) {
    sz_state_t *st = fa_calloc(1, sizeof *st);
    if (!st) return FULTA_ARC_ERR_NOMEM;
    arc->state = st;
    st->cur_folder = UINT32_MAX;
    const fulta_arc_source_t *first = arc->volumes[0];
    /* parts: name.7z.001, .002, ... */
    st->range.pieces = fa_calloc(1, sizeof *st->range.pieces);
    if (!st->range.pieces) return FULTA_ARC_ERR_NOMEM;
    st->range.pieces[0] = (fa_piece_t){first, 0, first->size};
    st->range.count = 1;
    st->range.size = first->size;
    const char *nm = arc->opt.name;
    size_t L = nm ? strlen(nm) : 0;
    if (nm && L > 4 && strcmp(nm + L - 4, ".001") == 0) {
        for (uint32_t k = 2; k < 100000; k++) {
            char *vn = fa_malloc(L + 8);
            if (!vn) return FULTA_ARC_ERR_NOMEM;
            memcpy(vn, nm, L - 3);
            snprintf(vn + L - 3, 11, "%03u", k);
            const fulta_arc_source_t *src;
            fulta_arc_err_t e = fa_volume(arc, k - 1, vn, &src);
            fa_free(vn);
            if (e == FULTA_ARC_ERR_VOLUME_MISSING) break;
            if (e) return e;
            fa_piece_t *p = fa_realloc(st->range.pieces, (st->range.count + 1) * sizeof *p);
            if (!p) return FULTA_ARC_ERR_NOMEM;
            st->range.pieces = p;
            p[st->range.count++] = (fa_piece_t){src, 0, src->size};
            st->range.size += src->size;
        }
    }
    /* signature: at 0, or searched in the first 4 MiB (SFX) */
    uint8_t sh[32];
    if (search == UINT64_MAX) {
        uint64_t limit = st->range.size < (4u << 20) ? st->range.size : (4u << 20);
        uint8_t *b = fa_malloc((size_t)limit);
        if (!b) return FULTA_ARC_ERR_NOMEM;
        fulta_arc_err_t e = fa_range_read_exact(&st->range, 0, b, (size_t)limit);
        if (e) { fa_free(b); return FULTA_ARC_ERR_NOT_ARCHIVE; }
        bool found = false;
        for (size_t i = 0; i + 32 <= limit; i++)
            if (memcmp(b + i, SIG, 6) == 0 && fa_crc32(0, b + i + 12, 20) == fa_le32(b + i + 8)) { st->base = i; found = true; break; }
        fa_free(b);
        if (!found) return FULTA_ARC_ERR_NOT_ARCHIVE;
    }
    fulta_arc_err_t e = fa_range_read_exact(&st->range, st->base, sh, 32);
    if (e) return e;
    if (memcmp(sh, SIG, 6) != 0) return FULTA_ARC_ERR_NOT_ARCHIVE;
    if (sh[6] != 0) return FULTA_ARC_ERR_UNSUPPORTED;
    if (fa_crc32(0, sh + 12, 20) != fa_le32(sh + 8)) return FULTA_ARC_ERR_CORRUPT;
    uint64_t nho = fa_le64(sh + 12), nhs = fa_le64(sh + 20);
    uint32_t nhc = fa_le32(sh + 28);
    if (nhs == 0) return FULTA_ARC_OK;   /* empty archive */
    if (nhs > MAX_HEADER || nho > st->range.size || st->base + 32 + nho + nhs > st->range.size) return FULTA_ARC_ERR_TRUNCATED;
    uint8_t *hdr = fa_malloc((size_t)nhs);
    if (!hdr) return FULTA_ARC_ERR_NOMEM;
    size_t hlen = (size_t)nhs;
    if ((e = fa_range_read_exact(&st->range, st->base + 32 + nho, hdr, hlen))) { fa_free(hdr); return e; }
    if (fa_crc32(0, hdr, hlen) != nhc) { fa_free(hdr); return FULTA_ARC_ERR_CORRUPT; }
    /* encoded headers (possibly encrypted): decode until a plain header appears */
    for (int round = 0; round < 4; round++) {
        if (hlen && hdr[0] == 0x01) break;
        if (!hlen || hdr[0] != 0x17) { fa_free(hdr); return FULTA_ARC_ERR_CORRUPT; }
        rd_t r = {hdr, hlen, 1, false};
        sz_streams_t ss = {0};
        e = parse_streams(&r, &ss);
        if (!e && ss.nfolders < 1) e = FULTA_ARC_ERR_CORRUPT;
        uint8_t *plain = NULL;
        size_t plen = 0;
        if (!e) {
            bool enc = ss.folders[0].encrypted;
            for (uint32_t attempt = 0;; attempt++) {
                st->key_valid = false;
                e = folder_to_memory(arc, &ss, 0, attempt, &plain, &plen);
                if (!enc || e == FULTA_ARC_OK || e == FULTA_ARC_ERR_PASSWORD_NEEDED || e == FULTA_ARC_ERR_NOMEM) break;
                if (attempt > 64) break;
                /* a wrong password shows up as bad data or a CRC mismatch: ask again */
                if (!arc->opt.password) { e = FULTA_ARC_ERR_PASSWORD_WRONG; break; }
            }
            if (e && enc && e != FULTA_ARC_ERR_PASSWORD_NEEDED && e != FULTA_ARC_ERR_NOMEM) e = FULTA_ARC_ERR_PASSWORD_WRONG;
            if (!e && enc && st->key_pw) fa_password_ok(arc, st->key_pw);
        }
        free_streams(&ss);
        fa_free(hdr);
        if (e) return e;
        hdr = plain;
        hlen = plen;
    }
    rd_t r = {hdr, hlen, 0, false};
    e = FULTA_ARC_OK;
    if (rd_byte(&r) != 0x01) e = FULTA_ARC_ERR_CORRUPT;
    uint64_t t = e ? 0 : rd_num(&r);
    if (!e && t == 0x02) {
        while (!r.bad && rd_num(&r) != 0) rd_skip(&r, rd_num(&r));
        t = rd_num(&r);
    }
    if (!e && t == 0x03) e = FULTA_ARC_ERR_UNSUPPORTED;
    if (!e && t == 0x04) {
        e = parse_streams(&r, &st->main);
        if (!e) t = rd_num(&r);
    }
    if (!e && t == 0x05) {
        e = parse_files(arc, &r);
        if (!e) t = rd_num(&r);
    }
    if (!e && (t != 0 || r.bad)) e = FULTA_ARC_ERR_CORRUPT;
    fa_free(hdr);
    return e;
}

/* ---- extraction ------------------------------------------------------------------------------------------- */

static fulta_arc_err_t sz_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    sz_state_t *st = arc->state;
    const sz_file_t *sf = &st->files[index];
    if (arc->entries[index].pub.flags & FULTA_ARC_ENTRY_UNSUPPORTED) return FULTA_ARC_ERR_UNSUPPORTED;
    if (sf->folder == UINT32_MAX) return FULTA_ARC_OK;
    const sz_folder_t *f = &st->main.folders[sf->folder];
    fulta_arc_err_t e;
    if (!(st->cur && st->cur_folder == sf->folder && st->cur_pos <= sf->offset)) {
        fa_stream_destroy(st->cur);
        st->cur = NULL;
        st->cur_folder = UINT32_MAX;
        e = open_folder(arc, &st->main, sf->folder, 0, &st->cur);
        if (e) return e;
        st->cur_folder = sf->folder;
        st->cur_pos = 0;
    }
    e = fa_stream_skip(st->cur, sf->offset - st->cur_pos);
    if (!e) {
        st->cur_pos = sf->offset;
        e = fa_pump(st->cur, sf->size, sf->has_crc, sf->crc, sink, arc);
    }
    if (!e) {
        st->cur_pos += sf->size;
        if (f->encrypted && st->key_pw) fa_password_ok(arc, st->key_pw);
        return FULTA_ARC_OK;
    }
    fa_stream_destroy(st->cur);
    st->cur = NULL;
    st->cur_folder = UINT32_MAX;
    /* 7zAES has no password check value: with an encrypted folder, bad data means a wrong password (crypto.md 3).
     * The next call asks the password callback again. */
    if (f->encrypted && (e == FULTA_ARC_ERR_CORRUPT || e == FULTA_ARC_ERR_CHECKSUM || e == FULTA_ARC_ERR_TRUNCATED)) {
        st->key_valid = false;
        return FULTA_ARC_ERR_PASSWORD_WRONG;
    }
    return e;
}

static void sz_close(fulta_arc_t *arc) {
    sz_state_t *st = arc->state;
    if (!st) return;
    fa_stream_destroy(st->cur);
    free_streams(&st->main);
    fa_free(st->files);
    fa_free(st->range.pieces);
    memset(st->key, 0, sizeof st->key);
    if (st->key_pw) { memset(st->key_pw, 0, strlen(st->key_pw)); fa_free(st->key_pw); }
    fa_free(st);
    arc->state = NULL;
}

const fa_format_ops_t fa_7z_ops = {FULTA_ARC_FORMAT_7Z, sz_probe, sz_open, sz_extract, sz_close};
