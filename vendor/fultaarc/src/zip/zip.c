/* zip.c - FultaArc: ZIP reader. MIT. Written from docs/specs/zip.md (FultaArc's format document: PKWARE's APPNOTE,
 * Info-ZIP's extra-field notes, WinZip's AES notes) and docs/specs/crypto.md. */
#include "../core/internal.h"

#include <stdio.h>

typedef struct zip_item {
    uint16_t flags, method, real_method, mtime, mdate, host;
    uint32_t crc;
    uint64_t csize, usize, local;      /* local = logical offset of the local header */
    uint8_t aes_strength, aes_version;
    uint16_t strong_alg, strong_bits;  /* PKWARE strong encryption (extra 0x0017), 0 when absent */
    bool encrypted;
} zip_item_t;

typedef struct zip_state {
    fa_range_t range;                  /* all disks joined (one piece for a single file) */
    zip_item_t *items;
    size_t nitems;
} zip_state_t;

static bool zip_probe(const uint8_t *h, size_t n, uint64_t *off) {
    *off = 0;
    if (n >= 4 && h[0] == 'P' && h[1] == 'K' &&
        ((h[2] == 3 && h[3] == 4) || (h[2] == 5 && h[3] == 6) || (h[2] == 7 && h[3] == 8) || (h[2] == '0' && h[3] == '0')))
        return true;
    return false;
}

/* PKWARE strong encryption, AES only (crypto.md 7): bit length matching the algorithm id, else 0. */
static size_t strong_key_len(uint16_t alg, uint16_t bits) {
    if (alg == 0x660E && bits == 128) return 16;
    if (alg == 0x660F && bits == 192) return 24;
    if (alg == 0x6610 && bits == 256) return 32;
    return 0;
}

static fulta_arc_err_t read_range(zip_state_t *z, uint64_t off, void *buf, size_t n) {
    return fa_range_read_exact(&z->range, off, buf, n);
}

/* Find the end record in the last `n` bytes (zip.md section 2). */
static fulta_arc_err_t find_eocd(const fulta_arc_source_t *src, uint64_t *pos, uint8_t eocd[22]) {
    uint64_t size = src->size;
    if (size < 22) return FULTA_ARC_ERR_NOT_ARCHIVE;
    size_t tail = size < 22 + 65535 ? (size_t)size : 22 + 65535;
    uint8_t *b = fa_malloc(tail);
    if (!b) return FULTA_ARC_ERR_NOMEM;
    fulta_arc_err_t e = fa_source_read_exact(src, size - tail, b, tail);
    if (e) { fa_free(b); return e; }
    e = FULTA_ARC_ERR_NOT_ARCHIVE;
    /* first a record whose comment reaches the end exactly; then, for bytes appended after the archive, the last
     * record whose comment fits (zip.md section 2) */
    for (int strict = 1; strict >= 0 && e; strict--)
        for (size_t i = tail - 22 + 1; i-- > 0;) {
            size_t end = i + 22 + fa_le16(b + i + 20);
            if (b[i] == 'P' && b[i + 1] == 'K' && b[i + 2] == 5 && b[i + 3] == 6 && (strict ? end == tail : end <= tail)) {
                memcpy(eocd, b + i, 22);
                *pos = size - tail + i;
                e = FULTA_ARC_OK;
                break;
            }
        }
    fa_free(b);
    return e;
}

static char *zip_name(fulta_arc_t *arc, const uint8_t *raw, size_t n, uint16_t flags, const uint8_t *ex, size_t exlen,
                      bool *not_unicode) {
    *not_unicode = false;
    if (flags & 0x0800) return fa_codepage_to_utf8(raw, n, FULTA_ARC_CP_UTF8, FULTA_ARC_CP_UTF8);
    /* Info-ZIP Unicode path extra field, used only when its CRC matches the header name (zip.md section 4) */
    for (size_t p = 0; p + 4 <= exlen;) {
        uint16_t id = fa_le16(ex + p), sz = fa_le16(ex + p + 2);
        if (p + 4 + sz > exlen) break;
        if (id == 0x7075 && sz >= 5 && ex[p + 4] == 1 && fa_le32(ex + p + 5) == fa_crc32(0, raw, n) &&
            fa_utf8_valid(ex + p + 9, sz - 5))
            return fa_strndup((const char *)ex + p + 9, sz - 5u);
        p += 4u + sz;
    }
    return fa_decode_name(arc, raw, n, false, FULTA_ARC_CP_437, not_unicode);
}

static const char *method_name(uint16_t m) {
    switch (m) {
    case 0: return "Store";
    case 1: return "Shrink";
    case 2: case 3: case 4: case 5: return "Reduce";
    case 6: return "Implode";
    case 8: return "Deflate";
    case 9: return "Deflate64";
    case 12: return "BZip2";
    case 14: return "LZMA";
    case 93: return "Zstandard";
    case 95: return "XZ";
    case 98: return "PPMd";
    default: return "Unknown";
    }
}

static bool method_supported(uint16_t m) { return m <= 6 || m == 8 || m == 9 || m == 12 || m == 14 || m == 93 || m == 95 || m == 98; }

static fulta_arc_err_t zip_open(fulta_arc_t *arc, uint64_t search) {
    if (search == UINT64_MAX) {
        /* a stub in front (SFX) is handled through the end record; nothing more to search for here */
    }
    const fulta_arc_source_t *last = arc->volumes[0];
    uint8_t eocd[22] = {0};
    uint64_t epos = 0;                        /* find_eocd fills both on success; zero-init keeps -O1 -Werror
                                                 (-Wmaybe-uninitialized can't correlate the OK return with the writes) */
    fulta_arc_err_t e = find_eocd(last, &epos, eocd);
    if (e) return e;
    uint32_t disk = fa_le16(eocd + 4), cd_disk = fa_le16(eocd + 6);
    uint64_t total = fa_le16(eocd + 10), cd_size = fa_le32(eocd + 12), cd_off = fa_le32(eocd + 16);
    uint64_t anchor = epos;                   /* where the central directory ends, in the last disk */
    bool z64 = false;
    if (epos >= 20) {
        uint8_t loc[20];
        if (!fa_source_read_exact(last, epos - 20, loc, 20) && loc[0] == 'P' && loc[1] == 'K' && loc[2] == 6 && loc[3] == 7) {
            uint64_t zoff = fa_le64(loc + 8);
            uint8_t rec[56];
            /* the record normally sits right before the locator; its stored offset is relative to its disk */
            uint64_t cand = epos >= 20 + 56 ? epos - 20 - 56 : 0;
            bool found = false;
            if (!fa_source_read_exact(last, cand, rec, 56) && fa_le32(rec) == 0x06064b50) found = true;
            else if (zoff + 56 <= last->size && !fa_source_read_exact(last, zoff, rec, 56) && fa_le32(rec) == 0x06064b50) {
                cand = zoff;
                found = true;
            }
            if (found) {
                z64 = true;
                disk = fa_le32(rec + 16);
                cd_disk = fa_le32(rec + 20);
                total = fa_le64(rec + 32);
                cd_size = fa_le64(rec + 40);
                cd_off = fa_le64(rec + 48);
                anchor = cand;
            }
        }
    }
    zip_state_t *z = fa_calloc(1, sizeof *z);
    if (!z) return FULTA_ARC_ERR_NOMEM;
    arc->state = z;

    /* disks: .z01 .. .zNN, then this file (zip.md 7.4) */
    uint32_t ndisks = disk + 1;
    if (ndisks > 4096) return FULTA_ARC_ERR_CORRUPT;
    z->range.pieces = fa_calloc(ndisks, sizeof *z->range.pieces);
    if (!z->range.pieces) return FULTA_ARC_ERR_NOMEM;
    uint64_t *disk_start = fa_calloc(ndisks, sizeof *disk_start);
    if (!disk_start) return FULTA_ARC_ERR_NOMEM;
    for (uint32_t d = 0; d < ndisks; d++) {
        const fulta_arc_source_t *src = last;
        if (d + 1 < ndisks) {
            char *vname = NULL;
            if (arc->opt.name) {
                size_t L = strlen(arc->opt.name);
                vname = fa_malloc(L + 8);
                if (!vname) { fa_free(disk_start); return FULTA_ARC_ERR_NOMEM; }
                memcpy(vname, arc->opt.name, L + 1);
                char *dot = strrchr(vname, '.');
                if (!dot || strchr(dot, '/') || strchr(dot, '\\')) dot = vname + L;
                snprintf(dot, 8, ".z%02u", (unsigned)(d + 1));
            }
            e = fa_volume(arc, d + 1, vname, &src);
            fa_free(vname);
            if (e) { fa_free(disk_start); return e; }
        }
        disk_start[d] = z->range.size;
        z->range.pieces[d] = (fa_piece_t){.src = src, .offset = 0, .size = src->size};
        z->range.size += src->size;
        z->range.count++;
    }
    /* prefix shift (SFX or data in front), and the recovery for a saturated offset without Zip64 (zip.md 7.1, 7.3) */
    uint64_t base = disk_start[cd_disk < ndisks ? cd_disk : ndisks - 1];
    uint64_t shift = 0;
    uint64_t cd_pos;
    if (ndisks == 1) {
        if (!z64 && cd_off == 0xFFFFFFFFu) {
            if (cd_size > anchor) { fa_free(disk_start); return FULTA_ARC_ERR_CORRUPT; }
            cd_pos = anchor - cd_size;
        } else {
            if (cd_off + cd_size > anchor) { fa_free(disk_start); return FULTA_ARC_ERR_CORRUPT; }
            shift = anchor - (cd_off + cd_size);
            cd_pos = cd_off + shift;
        }
    } else {
        cd_pos = base + cd_off;
    }
    if (cd_size > 0x40000000u || cd_pos + cd_size > z->range.size) { fa_free(disk_start); return FULTA_ARC_ERR_CORRUPT; }
    uint8_t *cd = fa_malloc((size_t)cd_size);
    if (!cd) { fa_free(disk_start); return FULTA_ARC_ERR_NOMEM; }
    e = read_range(z, cd_pos, cd, (size_t)cd_size);
    if (e) { fa_free(cd); fa_free(disk_start); return e; }
    if (total > cd_size / 46) total = cd_size / 46;   /* more entries than fit is corrupt; read what fits */
    z->items = fa_calloc(total ? total : 1, sizeof *z->items);
    if (!z->items) { fa_free(cd); fa_free(disk_start); return FULTA_ARC_ERR_NOMEM; }
    size_t p = 0;
    for (uint64_t i = 0; i < total; i++) {
        if (p + 46 > cd_size || fa_le32(cd + p) != 0x02014b50) { e = FULTA_ARC_ERR_CORRUPT; break; }
        const uint8_t *h = cd + p;
        zip_item_t it = {0};
        it.host = h[5];
        it.flags = fa_le16(h + 8);
        it.method = fa_le16(h + 10);
        it.mtime = fa_le16(h + 12);
        it.mdate = fa_le16(h + 14);
        it.crc = fa_le32(h + 16);
        it.csize = fa_le32(h + 20);
        it.usize = fa_le32(h + 24);
        uint16_t nlen = fa_le16(h + 28), xlen = fa_le16(h + 30), klen = fa_le16(h + 32);
        uint32_t dstart = fa_le16(h + 34);
        uint32_t ext_attr = fa_le32(h + 38);
        it.local = fa_le32(h + 42);
        if (p + 46u + nlen + xlen + klen > cd_size) { e = FULTA_ARC_ERR_CORRUPT; break; }
        const uint8_t *name = h + 46, *ex = h + 46 + nlen;
        int64_t mtime_ns = fa_dos_time_ns(it.mdate, it.mtime);
        for (size_t q = 0; q + 4 <= xlen;) {
            uint16_t id = fa_le16(ex + q), sz = fa_le16(ex + q + 2);
            const uint8_t *d = ex + q + 4;
            if (q + 4u + sz > xlen) break;
            if (id == 0x0001) {
                size_t r = 0;
                if (it.usize == 0xFFFFFFFFu && r + 8 <= sz) { it.usize = fa_le64(d + r); r += 8; }
                if (it.csize == 0xFFFFFFFFu && r + 8 <= sz) { it.csize = fa_le64(d + r); r += 8; }
                if (it.local == 0xFFFFFFFFu && r + 8 <= sz) { it.local = fa_le64(d + r); r += 8; }
                if (dstart == 0xFFFF && r + 4 <= sz) dstart = fa_le32(d + r);
            } else if (id == 0x5455 && sz >= 5 && (d[0] & 1)) {
                mtime_ns = (int64_t)(int32_t)fa_le32(d + 1) * 1000000000;
            } else if (id == 0x9901 && sz >= 7) {
                it.aes_version = (uint8_t)fa_le16(d);
                it.aes_strength = d[4];
                it.real_method = fa_le16(d + 5);
            } else if (id == 0x0017 && sz >= 8 && fa_le16(d + 6) == 1) {   /* password only, no certificates */
                it.strong_alg = fa_le16(d + 2);
                it.strong_bits = fa_le16(d + 4);
            }
            q += 4u + sz;
        }
        if (it.method != 99) it.real_method = it.method;
        it.encrypted = it.flags & 1;
        if (dstart >= ndisks) { e = FULTA_ARC_ERR_CORRUPT; break; }
        it.local += ndisks > 1 ? disk_start[dstart] : shift;
        bool name_nu = false;
        char *uname = zip_name(arc, name, nlen, it.flags, ex, xlen, &name_nu);
        if (!uname) { e = FULTA_ARC_ERR_NOMEM; break; }
        char mname[64];
        bool strong = (it.flags & 0x40) && strong_key_len(it.strong_alg, it.strong_bits);
        snprintf(mname, sizeof mname, "%s%s", method_name(it.real_method),
                 it.method == 99 ? ":AES" : strong ? ":PKAES" : (it.encrypted ? ":ZipCrypto" : ""));
        fa_entry_t *en = fa_add_entry(arc, name, nlen, uname, mname);
        fa_free(uname);
        if (!en) { e = FULTA_ARC_ERR_NOMEM; break; }
        en->pub.size = it.usize;
        en->pub.packed_size = it.csize;
        en->pub.crc32 = it.crc;
        en->pub.mtime = mtime_ns;
        en->pub.flags |= FULTA_ARC_ENTRY_HAS_MTIME;
        if (!(it.method == 99 && it.aes_version == 2)) en->pub.flags |= FULTA_ARC_ENTRY_HAS_CRC32;
        uint16_t made = fa_le16(h + 4);
        uint8_t os = (uint8_t)(made >> 8);
        en->pub.attributes = ext_attr;
        if (os == 3 || os == 19) {
            en->pub.unix_mode = ext_attr >> 16;
            if ((en->pub.unix_mode & 0xF000) == 0xA000) en->pub.flags |= FULTA_ARC_ENTRY_SYMLINK;
            if ((en->pub.unix_mode & 0xF000) == 0x4000) en->pub.flags |= FULTA_ARC_ENTRY_DIR;
        }
        if ((nlen && (name[nlen - 1] == '/' || name[nlen - 1] == '\\')) || ((os == 0 || os == 10 || os == 11 || os == 14) && (ext_attr & 0x10)))
            en->pub.flags |= FULTA_ARC_ENTRY_DIR;
        if (it.encrypted) en->pub.flags |= FULTA_ARC_ENTRY_ENCRYPTED;
        if (name_nu) en->pub.flags |= FULTA_ARC_ENTRY_NAME_NOT_UNICODE;
        if (ndisks > 1) en->pub.flags |= FULTA_ARC_ENTRY_SPLIT;
        if (!method_supported(it.real_method) || ((it.flags & 0x40) && !strong) || ((it.flags & 0x2000) && (it.flags & 0x40)) || (it.method == 99 && (it.aes_strength < 1 || it.aes_strength > 3)))
            en->pub.flags |= FULTA_ARC_ENTRY_UNSUPPORTED;
        z->items[z->nitems++] = it;
        p += 46u + nlen + xlen + klen;
    }
    fa_free(cd);
    fa_free(disk_start);
    if (e) return e;
    return FULTA_ARC_OK;
}

/* ---- editing: copy descriptors (src/write/edit.c) ---- */

fulta_arc_err_t fa_zip_copy_read(fulta_arc_t *arc, uint64_t off, void *buf, size_t n) {
    return read_range((zip_state_t *)arc->state, off, buf, n);
}

fulta_arc_err_t fa_zip_copy_info(fulta_arc_t *arc, size_t index, fa_zip_copy_t *out) {
    zip_state_t *z = arc->state;
    if (index >= z->nitems || index >= arc->count) return FULTA_ARC_ERR_INVALID_ARG;
    zip_item_t *it = &z->items[index];
    uint8_t lh[30];
    fulta_arc_err_t e = read_range(z, it->local, lh, sizeof lh);
    if (e) return e;
    if (fa_le32(lh) != 0x04034b50) return FULTA_ARC_ERR_CORRUPT;
    uint16_t lnlen = fa_le16(lh + 26), lxlen = fa_le16(lh + 28);
    const fulta_arc_entry_t *pub = &arc->entries[index].pub;
    *out = (fa_zip_copy_t){
        .method = it->method, .flags = it->flags, .time = it->mtime, .date = it->mdate,
        .made_by_host = pub->unix_mode ? 3 : 0, .crc = it->crc, .ext_attr = pub->attributes,
        .usize = it->usize, .csize = it->csize, .data_off = it->local + 30u + lnlen + lxlen,
        .name_raw = pub->name_raw, .name_raw_len = pub->name_raw_size,
        .mtime = (pub->flags & FULTA_ARC_ENTRY_HAS_MTIME) ? pub->mtime / 1000000000 : 0,
        .is_dir = (pub->flags & FULTA_ARC_ENTRY_DIR) != 0,
    };
    return FULTA_ARC_OK;
}

/* CryptDeriveKey with SHA-1: the hash, zero-padded to 64 bytes, XOR 0x36 and XOR 0x5C, each hashed; key = the two
 * digests joined, cut to its length. */
static void strong_derive(const uint8_t h[20], uint8_t *key, size_t kl) {
    uint8_t pad[64], out[40];
    for (int x = 0; x < 2; x++) {
        for (int i = 0; i < 64; i++) pad[i] = (uint8_t)((i < 20 ? h[i] : 0) ^ (x ? 0x5C : 0x36));
        fa_sha1_t c;
        fa_sha1_init(&c);
        fa_sha1_update(&c, pad, 64);
        fa_sha1_final(&c, out + 20 * x);
    }
    memcpy(key, out, kl);
}

static void cbc_decrypt(const uint8_t *key, size_t kl, uint8_t iv[16], uint8_t *buf, size_t n) {
    fa_aes_t a;
    fa_aes_init_dec(&a, key, kl);
    for (size_t p = 0; p + 16 <= n; p += 16) {
        uint8_t c[16];
        memcpy(c, buf + p, 16);
        fa_aes_decrypt(&a, c, buf + p);
        for (int i = 0; i < 16; i++) buf[p + i] ^= iv[i];
        memcpy(iv, c, 16);
    }
}

/* The decryption header at the start of the data (crypto.md 7): IVSize, IV, Size, Format 3, AlgId, BitLen, Flags,
 * ErdSize, Erd, RCount 0, VSize, VData. The master key comes from SHA-1(password); it opens the random data Erd;
 * the file key comes from SHA-1(IV + Erd unpadded). VData decrypted with the file key ends in the CRC-32 of the rest
 * (the password check), and the file data continues the same CBC chain. On success *hdr_len is the header's size
 * and key/iv are the file key and the IV for the data; FULTA_ARC_ERR_PASSWORD_WRONG when the check fails. */
static fulta_arc_err_t strong_begin(zip_state_t *z, const zip_item_t *it, uint64_t start, const char *pw, size_t kl,
                                    uint64_t *hdr_len, uint8_t key[32], uint8_t iv[16]) {
    uint8_t h[24];
    if (it->csize < 24) return FULTA_ARC_ERR_CORRUPT;
    fulta_arc_err_t e = read_range(z, start, h, 2);
    if (e) return e;
    if (fa_le16(h) != 16) return FULTA_ARC_ERR_UNSUPPORTED;      /* other IV sizes: not seen, not guessed */
    if ((e = read_range(z, start + 2, h + 2, 22))) return e;
    uint32_t size = fa_le32(h + 18);
    if (size < 14 + 4 + 2 || size > 0x10000 || 22 + (uint64_t)size > it->csize) return FULTA_ARC_ERR_CORRUPT;
    memcpy(iv, h + 2, 16);
    uint8_t *b = fa_malloc(size);
    if (!b) return FULTA_ARC_ERR_NOMEM;
    if ((e = read_range(z, start + 22, b, size))) { fa_free(b); return e; }
    e = FULTA_ARC_ERR_CORRUPT;
    uint16_t fmt = fa_le16(b), alg = fa_le16(b + 2), bits = fa_le16(b + 4), flags = fa_le16(b + 6), erds = fa_le16(b + 8);
    size_t q = 10;
    if (fmt != 3 || strong_key_len(alg, bits) != kl || flags != 1) { e = FULTA_ARC_ERR_UNSUPPORTED; goto done; }
    if (erds % 16 || !erds || q + erds + 6 > size) goto done;
    uint8_t *erd = b + q;
    q += erds;
    if (fa_le32(b + q) != 0) { e = FULTA_ARC_ERR_UNSUPPORTED; goto done; }   /* recipients: certificates */
    q += 4;
    uint16_t vs = fa_le16(b + q);
    q += 2;
    if (vs < 16 || vs % 16 || q + vs != size) goto done;
    uint8_t *vd = b + q, hash[20], mk[32], ivc[16];
    fa_sha1_t c;
    fa_sha1_init(&c);
    fa_sha1_update(&c, pw, strlen(pw));
    fa_sha1_final(&c, hash);
    strong_derive(hash, mk, kl);
    memcpy(ivc, iv, 16);
    cbc_decrypt(mk, kl, ivc, erd, erds);
    uint8_t pad = erd[erds - 1];
    size_t erdl = pad >= 1 && pad <= 16 ? erds - pad : erds;
    fa_sha1_init(&c);
    fa_sha1_update(&c, iv, 16);
    fa_sha1_update(&c, erd, erdl);
    fa_sha1_final(&c, hash);
    strong_derive(hash, key, kl);
    uint8_t last[16];
    memcpy(last, vd + vs - 16, 16);
    memcpy(ivc, iv, 16);
    cbc_decrypt(key, kl, ivc, vd, vs);
    if (fa_crc32(0, vd, vs - 4u) != fa_le32(vd + vs - 4)) { e = FULTA_ARC_ERR_PASSWORD_WRONG; goto done; }
    memcpy(iv, last, 16);
    *hdr_len = 22 + (uint64_t)size;
    e = FULTA_ARC_OK;
done:
    memset(b, 0, size);
    fa_free(b);
    memset(mk, 0, sizeof mk);
    return e;
}

static fulta_arc_err_t zip_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    zip_state_t *z = arc->state;
    zip_item_t *it = &z->items[index];
    fa_entry_t *en = &arc->entries[index];
    if (en->pub.flags & FULTA_ARC_ENTRY_UNSUPPORTED) return FULTA_ARC_ERR_UNSUPPORTED;
    uint8_t lh[30];
    fulta_arc_err_t e = read_range(z, it->local, lh, 30);
    if (e) return e;
    if (fa_le32(lh) != 0x04034b50) return FULTA_ARC_ERR_CORRUPT;
    uint64_t start = it->local + 30 + fa_le16(lh + 26) + fa_le16(lh + 28);
    uint64_t csize = it->csize;
    if (start > z->range.size || csize > z->range.size - start) return FULTA_ARC_ERR_TRUNCATED;
    fa_stream_t *s = NULL;
    uint8_t want_mac[10];
    if (it->encrypted) {
        for (uint32_t attempt = 0;; attempt++) {
            const char *pw;
            e = fa_password(arc, attempt, &pw);
            if (e) return attempt ? FULTA_ARC_ERR_PASSWORD_WRONG : e;
            size_t pwlen = strlen(pw);
            if (it->flags & 0x40) {
                size_t kl = strong_key_len(it->strong_alg, it->strong_bits);
                uint64_t hl;
                uint8_t key[32], iv[16];
                e = strong_begin(z, it, start, pw, kl, &hl, key, iv);
                if (e == FULTA_ARC_ERR_PASSWORD_WRONG) continue;
                if (e) return e;
                fa_password_ok(arc, pw);
                if ((e = fa_stream_range(&z->range, start + hl, csize - hl, &s))) return e;
                e = fa_dec_aes_cbc(s, key, kl, iv, &s);
                memset(key, 0, sizeof key);
                if (e) return e;
                break;
            }
            if (it->method == 99) {
                static const size_t salt_len[4] = {0, 8, 12, 16}, key_len[4] = {0, 16, 24, 32};
                size_t sl = salt_len[it->aes_strength], kl = key_len[it->aes_strength];
                if (csize < sl + 2 + 10) return FULTA_ARC_ERR_CORRUPT;
                uint8_t salt[16], pv[2], k[66];
                if ((e = read_range(z, start, salt, sl)) || (e = read_range(z, start + sl, pv, 2)) ||
                    (e = read_range(z, start + csize - 10, want_mac, 10)))
                    return e;
                fa_pbkdf2_sha1((const uint8_t *)pw, pwlen, salt, sl, 1000, k, 2 * kl + 2);
                if (k[2 * kl] != pv[0] || k[2 * kl + 1] != pv[1]) continue;
                fa_password_ok(arc, pw);
                /* the MAC covers all the ciphertext: check it in one pass before decoding (crypto.md 2) */
                fa_hmac_sha1_t mac;
                fa_hmac_sha1_init(&mac, k + kl, kl);
                if ((e = fa_stream_range(&z->range, start + sl + 2, csize - sl - 2 - 10, &s))) return e;
                if ((e = fa_stream_hmac_tap(s, &mac, &s))) return e;
                e = fa_pump(s, csize - sl - 2 - 10, false, 0, NULL, arc, false);
                fa_stream_destroy(s);
                s = NULL;
                if (e) return e;
                uint8_t m[20];
                fa_hmac_sha1_final(&mac, m);
                if (memcmp(m, want_mac, 10) != 0) return FULTA_ARC_ERR_CHECKSUM;
                if ((e = fa_stream_range(&z->range, start + sl + 2, csize - sl - 2 - 10, &s))) return e;
                if ((e = fa_dec_ctr(s, FA_CTR_AES_LE1, k, kl, &s))) return e;
                break;
            }
            if (csize < 12) return FULTA_ARC_ERR_CORRUPT;
            uint8_t hdr[12];
            if ((e = read_range(z, start, hdr, 12))) return e;
            fa_zipcrypto_t keys;
            fa_zipcrypto_init(&keys, (const uint8_t *)pw, pwlen);
            fa_zipcrypto_decrypt(&keys, hdr, 12);
            uint8_t want = (it->flags & 8) ? (uint8_t)(it->mtime >> 8) : (uint8_t)(it->crc >> 24);
            if (hdr[11] != want) continue;
            fa_password_ok(arc, pw);
            if ((e = fa_stream_range(&z->range, start + 12, csize - 12, &s))) return e;
            if ((e = fa_dec_zipcrypto(s, &keys, &s))) return e;
            break;
        }
    } else {
        if ((e = fa_stream_range(&z->range, start, csize, &s))) return e;
    }
    uint64_t usize = it->usize;
    /* the decoders own their input: on failure they destroy it and `s` stays NULL */
    fa_stream_t *raw = s;
    s = NULL;
    switch (it->real_method) {
    case 0: e = fa_stream_limit(raw, usize, &s); break;
    case 1: e = fa_dec_unshrink(raw, usize, &s); break;
    case 2: case 3: case 4: case 5: e = fa_dec_unreduce(raw, it->real_method - 1, usize, &s); break;
    case 6: {
        /* PKZIP 1.01 wrote 4 KiB entries with a literal tree using a minimum match of 2, not the APPNOTE's 3
         * (zip.md 6.4): for such an entry not encrypted, a CRC pass with 3 decides, else 2 is used */
        int min_len = 0;
        if ((it->flags & 6) == 4 && !it->encrypted) {
            fa_stream_t *t;
            if (!(e = fa_stream_range(&z->range, start, csize, &t)) && !(e = fa_dec_explode(t, it->flags, 3, usize, &t))) {
                e = fa_pump(t, usize, true, it->crc, NULL, arc, false);
                fa_stream_destroy(t);
            }
            if (e == FULTA_ARC_ERR_CHECKSUM || e == FULTA_ARC_ERR_CORRUPT || e == FULTA_ARC_ERR_TRUNCATED) min_len = 2;
            else if (e) { fa_stream_destroy(raw); break; }
        }
        e = fa_dec_explode(raw, it->flags, min_len, usize, &s);
        break;
    }
    case 8: e = fa_dec_deflate(raw, false, usize, &s); break;
    case 9: e = fa_dec_deflate(raw, true, usize, &s); break;
    case 12: e = fa_dec_bzip2(raw, usize, &s); break;
    case 14: {
        uint8_t h[4], props[5];
        e = fa_stream_read_exact(raw, h, 4);
        if (!e && fa_le16(h + 2) != 5) e = FULTA_ARC_ERR_UNSUPPORTED;
        if (!e) e = fa_stream_read_exact(raw, props, 5);
        if (e) { fa_stream_destroy(raw); break; }
        e = fa_dec_lzma(raw, props, usize, &arc->limits, &s);
        break;
    }
    case 93: e = fa_dec_zstd(raw, usize, &arc->limits, &s); break;
    case 98: e = fa_dec_ppmd8_zip(raw, usize, &arc->limits, &s); break;
    case 95:
        /* xz is read through its index at the end of the range: only for data not encrypted */
        fa_stream_destroy(raw);
        if (it->encrypted) { e = FULTA_ARC_ERR_UNSUPPORTED; break; }
        e = fa_dec_xz_range(&z->range, start, csize, &arc->limits, &s);
        if (!e) e = fa_stream_limit(s, usize, &s);
        break;
    default: fa_stream_destroy(raw); e = FULTA_ARC_ERR_UNSUPPORTED;
    }
    if (e) return e;
    bool has_crc = en->pub.flags & FULTA_ARC_ENTRY_HAS_CRC32;
    e = fa_pump(s, usize, has_crc, it->crc, sink, arc, true);
    fa_stream_destroy(s);
    return e;
}

static void zip_close(fulta_arc_t *arc) {
    zip_state_t *z = arc->state;
    if (!z) return;
    fa_free(z->range.pieces);
    fa_free(z->items);
    fa_free(z);
    arc->state = NULL;
}

/* Cheap password check (fulta_arc_check_password): match the format's check value without decoding the data. */
static fulta_arc_err_t zip_check_password(fulta_arc_t *arc, size_t index) {
    zip_state_t *z = arc->state;
    zip_item_t *it = &z->items[index];
    if (!it->encrypted) return FULTA_ARC_OK;
    uint8_t lh[30];
    fulta_arc_err_t e = read_range(z, it->local, lh, 30);
    if (e) return e;
    if (fa_le32(lh) != 0x04034b50) return FULTA_ARC_ERR_CORRUPT;
    uint64_t start = it->local + 30 + fa_le16(lh + 26) + fa_le16(lh + 28);
    uint64_t csize = it->csize;
    bool strong = (it->flags & 0x40) && strong_key_len(it->strong_alg, it->strong_bits);
    for (uint32_t attempt = 0;; attempt++) {
        const char *pw;
        e = fa_password(arc, attempt, &pw);
        if (e) return attempt ? FULTA_ARC_ERR_PASSWORD_WRONG : e;
        if (attempt > 64) return FULTA_ARC_ERR_PASSWORD_WRONG;
        size_t pwlen = strlen(pw);
        if (strong) {
            size_t kl = strong_key_len(it->strong_alg, it->strong_bits);
            uint64_t hl;
            uint8_t key[32], iv[16];
            e = strong_begin(z, it, start, pw, kl, &hl, key, iv);
            memset(key, 0, sizeof key);
            if (e == FULTA_ARC_ERR_PASSWORD_WRONG) continue;
            if (e) return e;
            fa_password_ok(arc, pw);
            return FULTA_ARC_OK;
        }
        if (it->method == 99) {
            static const size_t salt_len[4] = {0, 8, 12, 16}, key_len[4] = {0, 16, 24, 32};
            if (it->aes_strength < 1 || it->aes_strength > 3) return FULTA_ARC_ERR_UNSUPPORTED;
            size_t sl = salt_len[it->aes_strength], kl = key_len[it->aes_strength];
            if (csize < sl + 2) return FULTA_ARC_ERR_CORRUPT;
            uint8_t salt[16], pv[2], k[66];
            if ((e = read_range(z, start, salt, sl)) || (e = read_range(z, start + sl, pv, 2))) return e;
            fa_pbkdf2_sha1((const uint8_t *)pw, pwlen, salt, sl, 1000, k, 2 * kl + 2);
            if (k[2 * kl] == pv[0] && k[2 * kl + 1] == pv[1]) { fa_password_ok(arc, pw); return FULTA_ARC_OK; }
            continue;
        }
        if (csize < 12) return FULTA_ARC_ERR_CORRUPT;
        uint8_t hdr[12];
        if ((e = read_range(z, start, hdr, 12))) return e;
        fa_zipcrypto_t keys;
        fa_zipcrypto_init(&keys, (const uint8_t *)pw, pwlen);
        fa_zipcrypto_decrypt(&keys, hdr, 12);
        uint8_t want = (it->flags & 8) ? (uint8_t)(it->mtime >> 8) : (uint8_t)(it->crc >> 24);
        if (hdr[11] == want) { fa_password_ok(arc, pw); return FULTA_ARC_OK; }
    }
}

const fa_format_ops_t fa_zip_ops = {FULTA_ARC_FORMAT_ZIP, zip_probe, zip_open, zip_extract, zip_close, zip_check_password, NULL};
