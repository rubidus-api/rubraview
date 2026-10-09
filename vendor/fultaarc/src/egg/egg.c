/* egg.c - FultaArc: EGG reader. MIT.
 * Written from docs/specs/egg.md (FultaArc's EGG document: egg-verify's specification checked against egg-container's
 * and the sample bytes; both clean rooms worked from ALZip's output alone) and docs/specs/crypto.md 1, 4-6. It reads
 * a file's data in several blocks, the time as local wall-clock time, the attribute bits 0x01/0x02/0x80, and AZO
 * (docs/specs/azo.md) — the three gaps of egg-container's reader that the owner asked to close (D-005 #7, D-008). */
#include "../core/internal.h"

#include <stdio.h>

#define SIG_ARCHIVE 0x41474745u
#define SIG_END     0x08E28222u
#define SIG_FILE    0x0A8590E3u
#define SIG_NAME    0x0A8591ACu
#define SIG_TIME    0x2C86950Bu
#define SIG_CRYPT   0x08D1470Fu
#define SIG_BLOCK   0x02B50C13u
#define SIG_SOLID   0x24E5A060u
#define SIG_SPLIT   0x24F5A262u
#define SIG_COMMENT 0x04C63672u      /* egg.md 4.4: "description", UTF-8 (sub-record shape) */

typedef struct egg_block { uint8_t method; uint32_t out, in, crc; uint64_t data; } egg_block_t;

typedef struct egg_file {
    egg_block_t *blocks;
    size_t nblocks;
    uint8_t crypt;              /* 0xFF none */
    uint8_t cdata[29];
    uint64_t size, offset_in_solid;
} egg_file_t;

typedef struct egg_state {
    fa_range_t range;
    egg_file_t *files;
    bool solid;
    egg_block_t solid_block;
    /* sequential solid reading */
    fa_stream_t *cur;
    uint64_t cur_pos;
    uint32_t cur_crc;
} egg_state_t;

static bool egg_probe(const uint8_t *h, size_t n, uint64_t *off) {
    *off = 0;
    return n >= 4 && fa_le32(h) == SIG_ARCHIVE;
}

typedef struct cur { const fa_range_t *r; uint64_t pos; fulta_arc_err_t err; } cur_t;

static uint32_t c_u32(cur_t *c) {
    uint8_t b[4];
    if (!c->err) c->err = fa_range_read_exact(c->r, c->pos, b, 4);
    c->pos += 4;
    return c->err ? 0 : fa_le32(b);
}
static uint64_t c_u64(cur_t *c) { uint64_t lo = c_u32(c); return lo | ((uint64_t)c_u32(c) << 32); }
static uint8_t c_u8(cur_t *c) {
    uint8_t b = 0;
    if (!c->err) c->err = fa_range_read_exact(c->r, c->pos, &b, 1);
    c->pos++;
    return b;
}
static uint16_t c_u16(cur_t *c) { uint16_t lo = c_u8(c); return (uint16_t)(lo | (c_u8(c) << 8)); }

/* Sub-records up to END (egg.md 2). `cb` gets each; ALZip 8.6's encryption length quirk is handled there. */
typedef struct sub { uint32_t sig; uint8_t data[512]; uint16_t len; } sub_t;

static fulta_arc_err_t read_sub(cur_t *c, sub_t *s, bool *end) {
    s->sig = c_u32(c);
    if (c->err) return c->err;
    if (s->sig == SIG_END) { *end = true; return FULTA_ARC_OK; }
    *end = false;
    uint8_t flag = c_u8(c);
    uint16_t len = c_u16(c);
    if (c->err) return c->err;
    if (flag & 0x1F) return FULTA_ARC_ERR_UNSUPPORTED;               /* egg.md: ALZip reacts only to the low 5 bits */
    if (s->sig == SIG_CRYPT) {
        uint8_t m = c_u8(c);
        c->pos--;
        uint16_t want = m == 0 ? 17 : (m == 1 || m == 5) ? 21 : (m == 2 || m == 6) ? 29 : 0;
        if (!want) return FULTA_ARC_ERR_UNSUPPORTED;                  /* method bytes 3, 4 and others: unobserved */
        if (len != want && len - 7 == want) len = want;              /* ALZip 8.6 counts the 7-byte header */
        if (len != want) return FULTA_ARC_ERR_CORRUPT;
    }
    s->len = len;
    if (len <= sizeof s->data) {
        if (!c->err) c->err = fa_range_read_exact(c->r, c->pos, s->data, len);
    }
    c->pos += len;
    return c->err;
}

static fulta_arc_err_t read_block(cur_t *c, egg_block_t *b) {
    b->method = c_u8(c);
    (void)c_u8(c);                                                    /* level: informational */
    b->out = c_u32(c);
    b->in = c_u32(c);
    b->crc = c_u32(c);
    if (c_u32(c) != SIG_END && !c->err) return FULTA_ARC_ERR_CORRUPT;
    b->data = c->pos;
    c->pos += b->in;
    if (c->pos > c->r->size) return FULTA_ARC_ERR_TRUNCATED;
    return c->err;
}

/* egg.md 8.1: a self-extracting EGG is a program (starts with "MZ") followed by an ordinary EGG archive running to
 * the end of the file. Find the archive header: EGGA, a non-zero id, a zero reserved field, then END or the solid
 * or split sub-record - the checks skip the bare "EGGA" letters the program itself contains. */
static fulta_arc_err_t egg_find_header(const fulta_arc_source_t *src, uint64_t *base) {
    uint8_t mz[2];
    if (src->size < 18 || fa_source_read_exact(src, 0, mz, 2) || mz[0] != 'M' || mz[1] != 'Z')
        return FULTA_ARC_ERR_NOT_ARCHIVE;
    enum { CH = 16384, OV = 18 };
    uint8_t buf[CH + OV];
    for (uint64_t pos = 0; pos + 18 <= src->size;) {
        size_t want = (size_t)(src->size - pos < CH + OV ? src->size - pos : CH + OV);
        if (fa_source_read_exact(src, pos, buf, want)) return FULTA_ARC_ERR_NOT_ARCHIVE;
        size_t limit = want >= 18 ? want - 18 + 1 : 0;
        for (size_t i = 0; i < limit; i++) {
            if (fa_le32(buf + i) != SIG_ARCHIVE || fa_le32(buf + i + 6) == 0 || fa_le32(buf + i + 10) != 0) continue;
            uint32_t s14 = fa_le32(buf + i + 14);
            if (s14 == SIG_END || s14 == SIG_SOLID || s14 == SIG_SPLIT) { *base = pos + i; return FULTA_ARC_OK; }
        }
        if (want < CH + OV) break;
        pos += CH;
    }
    return FULTA_ARC_ERR_NOT_ARCHIVE;
}

static fulta_arc_err_t join_volumes(fulta_arc_t *arc, egg_state_t *st, uint64_t base) {
    const fulta_arc_source_t *first = arc->volumes[0];
    st->range.pieces = fa_calloc(1, sizeof *st->range.pieces);
    if (!st->range.pieces) return FULTA_ARC_ERR_NOMEM;
    st->range.pieces[0] = (fa_piece_t){first, base, first->size - base};
    st->range.count = 1;
    st->range.size = first->size - base;
    uint8_t h[33];
    if (first->size - base < 33 || fa_source_read_exact(first, base, h, 33)) return FULTA_ARC_OK;
    if (fa_le32(h + 14) != SIG_SPLIT) return FULTA_ARC_OK;            /* not a split archive */
    /* first volume: NAME.vol1.egg, or NAME.vol1.exe for a self-extracting split set (egg.md 8.1) */
    const char *nm = arc->opt.name;
    size_t L = nm ? strlen(nm) : 0;
    if (!nm || L < 9 || (strcmp(nm + L - 9, ".vol1.egg") != 0 && strcmp(nm + L - 9, ".vol1.exe") != 0))
        return FULTA_ARC_OK;
    uint32_t prev_id = fa_le32(h + 6), next = fa_le32(h + 25);
    for (uint32_t k = 2; next && k < 100000; k++) {
        char *vn = fa_malloc(L + 16);
        if (!vn) return FULTA_ARC_ERR_NOMEM;
        snprintf(vn, L + 16, "%.*s.vol%u.egg", (int)(L - 9), nm, (unsigned)k);
        const fulta_arc_source_t *src;
        fulta_arc_err_t e = fa_volume(arc, k - 1, vn, &src);
        fa_free(vn);
        if (e) return e;
        uint8_t v[33];
        if ((e = fa_source_read_exact(src, 0, v, 33))) return e;
        if (fa_le32(v) != SIG_ARCHIVE || fa_le32(v + 14) != SIG_SPLIT || fa_le32(v + 21) != prev_id ||
            fa_le32(v + 6) != next)
            return FULTA_ARC_ERR_CORRUPT;                             /* the id chain (egg.md 7) */
        fa_piece_t *p = fa_realloc(st->range.pieces, (st->range.count + 1) * sizeof *p);
        if (!p) return FULTA_ARC_ERR_NOMEM;
        st->range.pieces = p;
        p[st->range.count++] = (fa_piece_t){src, 33, src->size - 33};
        st->range.size += src->size - 33;
        prev_id = fa_le32(v + 6);
        next = fa_le32(v + 25);
    }
    return FULTA_ARC_OK;
}

static fulta_arc_err_t egg_open(fulta_arc_t *arc, uint64_t search) {
    uint64_t base = 0;
    if (search == UINT64_MAX) {                                     /* self-extracting EGG (egg.md 8.1) */
        fulta_arc_err_t fe = egg_find_header(arc->volumes[0], &base);
        if (fe) return fe;
    }
    egg_state_t *st = fa_calloc(1, sizeof *st);
    if (!st) return FULTA_ARC_ERR_NOMEM;
    arc->state = st;
    fulta_arc_err_t e = join_volumes(arc, st, base);
    if (e) return e;
    cur_t c = {&st->range, 0, FULTA_ARC_OK};
    if (c_u32(&c) != SIG_ARCHIVE) return c.err ? c.err : FULTA_ARC_ERR_NOT_ARCHIVE;
    (void)c_u16(&c);
    (void)c_u32(&c);
    (void)c_u32(&c);
    sub_t s;
    bool end = false;
    for (;;) {
        if ((e = read_sub(&c, &s, &end))) return e;
        if (end) break;
        if (s.sig == SIG_SOLID) st->solid = true;
    }
    size_t cap = 0;
    uint64_t solid_total = 0;
    for (;;) {
        uint32_t sig = c_u32(&c);
        if (c.err) return c.err;
        if (sig == SIG_END) break;
        if (sig == SIG_BLOCK && st->solid) {
            if (st->solid_block.out || st->solid_block.in) return FULTA_ARC_ERR_UNSUPPORTED;   /* several: unobserved */
            if ((e = read_block(&c, &st->solid_block))) return e;
            if (st->solid_block.out != solid_total) return FULTA_ARC_ERR_CORRUPT;
            continue;
        }
        if (sig == SIG_COMMENT) {                                     /* egg.md 4.4: archive description before END */
            uint8_t cflag = c_u8(&c);
            uint16_t clen = c_u16(&c);
            if (c.err) return c.err;
            if (cflag & 0x1F) return FULTA_ARC_ERR_UNSUPPORTED;
            if (c.pos + clen > st->range.size) return FULTA_ARC_ERR_CORRUPT;
            c.pos += clen;                                            /* text is UTF-8; exposing it is a future API */
            continue;
        }
        if (sig != SIG_FILE) return FULTA_ARC_ERR_CORRUPT;
        (void)c_u32(&c);                                              /* file index */
        uint64_t size = c_u64(&c);
        char *name = NULL;
        size_t nlen = 0;
        uint8_t namebuf[512];
        uint64_t ft = 0;
        uint8_t attr = 0;
        bool has_time = false;
        egg_file_t f = {.crypt = 0xFF, .size = size, .offset_in_solid = solid_total};
        for (;;) {
            if ((e = read_sub(&c, &s, &end))) { fa_free(name); return e; }
            if (end) break;
            if (s.sig == SIG_NAME && s.len <= sizeof namebuf) { memcpy(namebuf, s.data, s.len); nlen = s.len; }
            else if (s.sig == SIG_TIME && s.len == 9) { ft = fa_le64(s.data); attr = s.data[8]; has_time = true; }
            else if (s.sig == SIG_CRYPT) { f.crypt = s.data[0]; memcpy(f.cdata, s.data + 1, s.len - 1u); }
        }
        if (!st->solid) {
            uint64_t got = 0;
            while (got < size) {
                if (c_u32(&c) != SIG_BLOCK) { fa_free(f.blocks); return c.err ? c.err : FULTA_ARC_ERR_CORRUPT; }
                egg_block_t b;
                if ((e = read_block(&c, &b))) { fa_free(f.blocks); return e; }
                egg_block_t *nb = fa_realloc(f.blocks, (f.nblocks + 1) * sizeof *nb);
                if (!nb) { fa_free(f.blocks); return FULTA_ARC_ERR_NOMEM; }
                f.blocks = nb;
                f.blocks[f.nblocks++] = b;
                got += b.out;
                if (!b.out) { fa_free(f.blocks); return FULTA_ARC_ERR_CORRUPT; }
            }
            if (got != size) { fa_free(f.blocks); return FULTA_ARC_ERR_CORRUPT; }
        } else {
            solid_total += size;
        }
        name = fa_codepage_to_utf8(namebuf, nlen, FULTA_ARC_CP_UTF8, FULTA_ARC_CP_UTF8);
        if (!name) { fa_free(f.blocks); return FULTA_ARC_ERR_NOMEM; }
        char method[48] = "";
        uint8_t m = st->solid ? 0xFF : (f.nblocks ? f.blocks[0].method : 0);
        const char *mn = m == 0 ? "Store" : m == 1 ? "Deflate" : m == 2 ? "BZip2" : m == 3 ? "AZO" : m == 4 ? "LZMA" : "";
        const char *cn = f.crypt == 0 ? ":ZipCrypto" : f.crypt == 1 ? ":AES128" : f.crypt == 2 ? ":AES256"
                       : f.crypt == 5 ? ":LEA128" : f.crypt == 6 ? ":LEA256" : "";
        snprintf(method, sizeof method, "%s%s", mn, cn);
        fa_entry_t *en = fa_add_entry(arc, namebuf, nlen, name, method);
        fa_free(name);
        if (!en) { fa_free(f.blocks); return FULTA_ARC_ERR_NOMEM; }
        en->pub.size = size;
        if (has_time) {
            /* egg.md 4.2: a FILETIME count of the local wall-clock time, whole seconds */
            en->pub.mtime = fa_filetime_ns(ft);
            en->pub.flags |= FULTA_ARC_ENTRY_HAS_MTIME;
        }
        /* egg.md 4.2: 0x01 read-only, 0x02 hidden, 0x04 system, 0x80 folder -> Windows attribute bits */
        en->pub.attributes = (attr & 0x01 ? 0x01u : 0) | (attr & 0x02 ? 0x02u : 0)
                           | (attr & 0x04 ? 0x04u : 0) | (attr & 0x80 ? 0x10u : 0);
        if (attr & 0x80) en->pub.flags |= FULTA_ARC_ENTRY_DIR;
        if (f.crypt != 0xFF) en->pub.flags |= FULTA_ARC_ENTRY_ENCRYPTED;
        if (st->solid) en->pub.flags |= FULTA_ARC_ENTRY_SOLID;
        if (st->range.count > 1) en->pub.flags |= FULTA_ARC_ENTRY_SPLIT;
        if (f.nblocks == 1) { en->pub.crc32 = f.blocks[0].crc; en->pub.flags |= FULTA_ARC_ENTRY_HAS_CRC32; }
        uint64_t packed = 0;
        for (size_t k = 0; k < f.nblocks; k++) {
            packed += f.blocks[k].in;
            if (f.blocks[k].method > 4) en->pub.flags |= FULTA_ARC_ENTRY_UNSUPPORTED;
        }
        en->pub.packed_size = packed;
        if (f.crypt != 0xFF && f.nblocks > 1) en->pub.flags |= FULTA_ARC_ENTRY_UNSUPPORTED;   /* unobserved */
        if (arc->count > cap) {
            size_t nc = cap ? cap * 2 : 64;
            egg_file_t *nf = fa_realloc(st->files, nc * sizeof *nf);
            if (!nf) { fa_free(f.blocks); return FULTA_ARC_ERR_NOMEM; }
            memset(nf + cap, 0, (nc - cap) * sizeof *nf);
            st->files = nf;
            cap = nc;
        }
        st->files[arc->count - 1] = f;
    }
    if (st->solid && solid_total && !st->solid_block.in && !st->solid_block.out) return FULTA_ARC_ERR_CORRUPT;
    if (st->solid && st->solid_block.method > 4) {
        for (size_t i = 0; i < arc->count; i++) arc->entries[i].pub.flags |= FULTA_ARC_ENTRY_UNSUPPORTED;
    }
    if (c.pos != st->range.size) return FULTA_ARC_ERR_CORRUPT;      /* nothing follows the final END */
    return FULTA_ARC_OK;
}

/* Decryption (crypto.md 1, 4, 5). On success *s is the decrypting stream over the block's data; on failure *s has
 * been destroyed, as the decoders do with their input. */
static fulta_arc_err_t decrypt_open(fulta_arc_t *arc, const egg_file_t *f, const egg_block_t *b, fa_stream_t **s,
                                    bool *handed);

static fulta_arc_err_t decrypt(fulta_arc_t *arc, const egg_file_t *f, const egg_block_t *b, fa_stream_t **s) {
    bool handed = false;
    fulta_arc_err_t e = decrypt_open(arc, f, b, s, &handed);
    if (e && !handed) { fa_stream_destroy(*s); *s = NULL; }
    return e;
}

static fulta_arc_err_t decrypt_open(fulta_arc_t *arc, const egg_file_t *f, const egg_block_t *b, fa_stream_t **s,
                                    bool *handed) {
    for (uint32_t attempt = 0;; attempt++) {
        const char *pw;
        fulta_arc_err_t e = fa_password(arc, attempt, &pw);
        if (e) return attempt ? FULTA_ARC_ERR_PASSWORD_WRONG : e;
        if (attempt > 64) return FULTA_ARC_ERR_PASSWORD_WRONG;
        /* ALZip stores a password as the writer's CP949 bytes, never UTF-8 (egg.md 5, reader-note 3): try the
         * CP949 encoding first, then the raw UTF-8 as a courtesy. A password outside CP949 does not encode
         * (NULL) and only UTF-8 is tried. */
        char *cp949 = fa_utf8_to_codepage(pw, FULTA_ARC_CP_949);
        const char *cand[2];
        size_t nc = 0;
        if (cp949) cand[nc++] = cp949;
        cand[nc++] = pw;
        for (size_t ci = 0; ci < nc; ci++) {
            const uint8_t *pwb = (const uint8_t *)cand[ci];
            size_t pl = strlen(cand[ci]);
            if (f->crypt == 0) {
                fa_zipcrypto_t k;
                fa_zipcrypto_init(&k, pwb, pl);
                uint8_t h[12];
                memcpy(h, f->cdata, 12);
                fa_zipcrypto_decrypt(&k, h, 12);
                if (h[11] != (uint8_t)(fa_le32(f->cdata + 12) >> 24)) continue;
                fa_password_ok(arc, pw);
                fa_free(cp949);
                *handed = true;
                return fa_dec_zipcrypto(*s, &k, s);
            }
            size_t kl = (f->crypt == 1 || f->crypt == 5) ? 16 : 32, sl = kl == 16 ? 8 : 16;
            uint8_t key[66];
            fa_pbkdf2_sha1(pwb, pl, f->cdata, sl, 1000, key, 2 * kl + 2);
            if (key[2 * kl] != f->cdata[sl] || key[2 * kl + 1] != f->cdata[sl + 1]) continue;
            fa_password_ok(arc, pw);
            fa_free(cp949);
            /* MAC over the ciphertext, checked in a pass before decoding */
            fa_hmac_sha1_t mac;
            fa_hmac_sha1_init(&mac, key + kl, kl);
            fa_stream_t *t;
            if ((e = fa_stream_range(&((egg_state_t *)arc->state)->range, b->data, b->in, &t))) return e;
            if ((e = fa_stream_hmac_tap(t, &mac, &t))) return e;
            e = fa_pump(t, b->in, false, 0, NULL, arc, false);
            fa_stream_destroy(t);
            if (e) return e;
            uint8_t m[20];
            fa_hmac_sha1_final(&mac, m);
            if (memcmp(m, f->cdata + sl + 2, 10) != 0) return FULTA_ARC_ERR_CHECKSUM;
            *handed = true;
            return fa_dec_ctr(*s, f->crypt <= 2 ? FA_CTR_AES_LE1 : FA_CTR_LEA_BE0, key, kl, s);
        }
        fa_free(cp949);
    }
}

static fulta_arc_err_t decoder(fulta_arc_t *arc, const egg_block_t *b, fa_stream_t *in, fa_stream_t **out) {
    switch (b->method) {
    case 0: return fa_stream_limit(in, b->out, out);
    case 1: return fa_dec_deflate(in, false, b->out, out);
    case 2: return fa_dec_bzip2(in, b->out, out);
    case 3: {
        fulta_arc_err_t e = fa_dec_azo(in, &in);
        if (e) return e;
        return fa_stream_limit_strict(in, b->out, out);   /* validate AZO's end record even though we stop at b->out */
    }
    case 4: {
        uint8_t h[4], props[5];
        fulta_arc_err_t e = fa_stream_read_exact(in, h, 4);
        if (!e && fa_le16(h + 2) != 5) e = FULTA_ARC_ERR_UNSUPPORTED;
        if (!e) e = fa_stream_read_exact(in, props, 5);
        if (e) { fa_stream_destroy(in); return e; }
        return fa_dec_lzma(in, props, b->out, &arc->limits, out);
    }
    default: fa_stream_destroy(in); return FULTA_ARC_ERR_UNSUPPORTED;
    }
}

typedef struct crc_sink { const fulta_arc_sink_t *inner; uint32_t crc; } crc_sink_t;

static fulta_arc_err_t crc_write(void *ctx, const void *d, size_t n) {
    crc_sink_t *c = ctx;
    c->crc = fa_crc32(c->crc, d, n);
    return c->inner && c->inner->write ? c->inner->write(c->inner->ctx, d, n) : FULTA_ARC_OK;
}

static fulta_arc_err_t egg_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    egg_state_t *st = arc->state;
    egg_file_t *f = &st->files[index];
    if (arc->entries[index].pub.flags & FULTA_ARC_ENTRY_UNSUPPORTED) return FULTA_ARC_ERR_UNSUPPORTED;
    fulta_arc_err_t e;
    if (!st->solid) {
        for (size_t k = 0; k < f->nblocks; k++) {
            const egg_block_t *b = &f->blocks[k];
            fa_stream_t *s;
            if ((e = fa_stream_range(&st->range, b->data, b->in, &s))) return e;
            if (f->crypt != 0xFF && (e = decrypt(arc, f, b, &s))) return e;
            if ((e = decoder(arc, b, s, &s))) return e;
            e = fa_pump(s, b->out, true, b->crc, sink, arc, true);           /* each block has its own CRC (egg.md 5) */
            fa_stream_destroy(s);
            if (e) return e;
        }
        return FULTA_ARC_OK;
    }
    /* solid: one block for all files, read in order; the CRC covers the whole block */
    if (!st->cur || st->cur_pos > f->offset_in_solid) {
        fa_stream_destroy(st->cur);
        st->cur = NULL;
        fa_stream_t *s;
        if ((e = fa_stream_range(&st->range, st->solid_block.data, st->solid_block.in, &s))) return e;
        if ((e = decoder(arc, &st->solid_block, s, &st->cur))) return e;
        st->cur_pos = 0;
        st->cur_crc = 0;
    }
    crc_sink_t skip = {NULL, st->cur_crc};
    fulta_arc_sink_t sk = {&skip, crc_write};
    if ((e = fa_pump(st->cur, f->offset_in_solid - st->cur_pos, false, 0, &sk, arc, false))) goto fail;
    crc_sink_t cs = {sink, skip.crc};
    fulta_arc_sink_t ss = {&cs, crc_write};
    if ((e = fa_pump(st->cur, f->size, false, 0, &ss, arc, true))) goto fail;
    st->cur_pos = f->offset_in_solid + f->size;
    st->cur_crc = cs.crc;
    if (st->cur_pos == st->solid_block.out && st->cur_crc != st->solid_block.crc) return FULTA_ARC_ERR_CHECKSUM;
    return FULTA_ARC_OK;
fail:
    fa_stream_destroy(st->cur);
    st->cur = NULL;
    return e;
}

static void egg_close(fulta_arc_t *arc) {
    egg_state_t *st = arc->state;
    if (!st) return;
    fa_stream_destroy(st->cur);
    for (size_t i = 0; i < arc->count && st->files; i++) fa_free(st->files[i].blocks);
    fa_free(st->files);
    fa_free(st->range.pieces);
    fa_free(st);
    arc->state = NULL;
}

const fa_format_ops_t fa_egg_ops = {FULTA_ARC_FORMAT_EGG, egg_probe, egg_open, egg_extract, egg_close, NULL, NULL};
