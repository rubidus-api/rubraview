/* tar.c - FultaArc: TAR (Unix tape archive) reader - ustar, POSIX pax, GNU, v7. MIT.
 * Original FultaArc code, MIT-licensed. The format facts are the normative POSIX.1 standard ("ustar Interchange
 * Format" and "pax Interchange Format", freely published by IEEE/The Open Group); the GNU extensions (the L/K long
 * name/link typeflags, base-256 numeric fields, the "ustar  \0" magic) are public format facts also described in the
 * permissively (BSD) licensed FreeBSD tar(5) manual. No GPL/LGPL implementation - GNU tar, libarchive, star - was
 * read or copied; this reader was written only from those specifications (docs/research/formats/tar.md). A tar
 * archive is a sequence of 512-byte blocks: each member is a 512-byte header followed by its data, NUL-padded to the
 * next 512 boundary, ending at two zero blocks or EOF. Tar stores no compression, no encryption and no per-entry
 * checksum, so this reader lists members and streams their stored bytes to the sink. No writer; read-only. See D-020. */
#include "../core/internal.h"

#define BLK 512
#define NAME_MAX_META (1u << 20)   /* cap a GNU longname / pax record set before allocating (untrusted input) */

/* per-member record, parallel to arc->entries */
typedef struct tar_item {
    uint64_t off;        /* data offset in the source (for a regular file) */
    uint64_t size;       /* data size */
    char *link;          /* non-NULL for a symlink: the target bytes to emit as the "data" */
} tar_item_t;

typedef struct tar_state {
    tar_item_t *items;
    size_t n, cap;
} tar_state_t;

/* Parse a tar numeric field: octal (space/NUL terminated), or GNU base-256 when the top bit of byte 0 is set. */
static bool tar_num(const uint8_t *f, size_t len, uint64_t *out) {
    if (len == 0) { *out = 0; return true; }
    if (f[0] & 0x80) {                                   /* GNU base-256, big-endian, top bit of byte 0 cleared */
        uint64_t v = (uint64_t)(f[0] & 0x7F);
        for (size_t i = 1; i < len; i++) v = (v << 8) | f[i];
        *out = v;
        return true;
    }
    uint64_t v = 0;
    size_t i = 0;
    while (i < len && (f[i] == ' ' || f[i] == 0)) i++;   /* leading blanks */
    bool any = false;
    for (; i < len && f[i] >= '0' && f[i] <= '7'; i++) { v = (v << 3) | (uint64_t)(f[i] - '0'); any = true; }
    while (i < len && (f[i] == ' ' || f[i] == 0)) i++;
    if (i < len) return false;                           /* a non-octal, non-terminator byte: not a number */
    *out = v;
    return any || true;                                  /* an all-blank field is 0 */
}

/* The ustar checksum: the unsigned sum of all 512 bytes with the 8 chksum bytes counted as spaces. */
static bool tar_checksum_ok(const uint8_t *h) {
    uint64_t stored;
    if (!tar_num(h + 148, 8, &stored)) return false;
    uint32_t sum = 0;
    for (int i = 0; i < BLK; i++) sum += (i >= 148 && i < 156) ? 0x20u : h[i];
    return sum == stored;
}

static bool tar_all_zero(const uint8_t *h) {
    for (int i = 0; i < BLK; i++) if (h[i]) return false;
    return true;
}

/* The reprobe layer's "is this a tar?" gate on a decompressed prefix. */
bool fa_tar_detect(const uint8_t *h, size_t n) {
    return n >= BLK && !tar_all_zero(h) && tar_checksum_ok(h);
}

static tar_item_t *tar_add_item(tar_state_t *st) {
    if (st->n == st->cap) {
        size_t nc = st->cap ? st->cap * 2 : 32;
        tar_item_t *p = fa_realloc(st->items, nc * sizeof *p);
        if (!p) return NULL;
        st->items = p;
        st->cap = nc;
    }
    tar_item_t *it = &st->items[st->n++];
    *it = (tar_item_t){0};
    return it;
}

/* Decimal (pax values are base-10 ASCII), optional leading '-'. */
static int64_t tar_dec(const char *s) {
    bool neg = *s == '-';
    if (neg) s++;
    uint64_t v = 0;
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (uint64_t)(*s - '0');
    return neg ? -(int64_t)v : (int64_t)v;
}

/* Find value for `key` in a pax record block ("<len> key=value\n" entries). Returns a heap copy or NULL. */
static char *pax_get(const uint8_t *rec, size_t reclen, const char *key) {
    size_t klen = strlen(key);
    size_t i = 0;
    while (i < reclen) {
        size_t j = i;
        uint64_t len = 0;
        int digits = 0;
        while (j < reclen && rec[j] >= '0' && rec[j] <= '9') { len = len * 10 + (uint64_t)(rec[j] - '0'); j++; digits++; }
        if (digits == 0 || digits > 18 || j >= reclen || rec[j] != ' ' || len < (size_t)(j - i) + 1 || len > reclen - i)
            break;  /* malformed length (≤18 digits avoids uint64 wrap) */
        size_t end = i + (size_t)len;                    /* one past the record's trailing '\n' */
        j++;                                             /* past the space */
        /* rec[j .. end-1] is "key=value\n" */
        if (end > j && rec[end - 1] == '\n' && (size_t)(end - 1 - j) > klen &&
            memcmp(rec + j, key, klen) == 0 && rec[j + klen] == '=') {
            const uint8_t *val = rec + j + klen + 1;
            size_t vlen = (size_t)(end - 1) - (j + klen + 1);
            char *s = fa_malloc(vlen + 1);
            if (!s) return NULL;
            memcpy(s, val, vlen);
            s[vlen] = 0;
            return s;
        }
        i = end;
    }
    return NULL;
}

static void tar_close(fulta_arc_t *arc);

static fulta_arc_err_t tar_open(fulta_arc_t *arc, uint64_t offset) {
    (void)offset;
    const fulta_arc_source_t *src = arc->volumes[0];
    if (src->size < BLK) return FULTA_ARC_ERR_NOT_ARCHIVE;

    uint8_t h[BLK];
    if (fa_source_read_exact(src, 0, h, BLK)) return FULTA_ARC_ERR_NOT_ARCHIVE;
    if (!fa_tar_detect(h, BLK)) return FULTA_ARC_ERR_NOT_ARCHIVE;   /* the detection gate */

    tar_state_t *st = fa_calloc(1, sizeof *st);
    if (!st) return FULTA_ARC_ERR_NOMEM;
    arc->state = st;

    char *long_name = NULL, *long_link = NULL;   /* pending GNU 'L'/'K' overrides for the next member */
    char *pax_path = NULL, *pax_link = NULL;      /* pending pax 'x' overrides */
    uint64_t pax_size = 0;
    bool pax_has_size = false;
    int64_t pax_mtime = 0;
    bool pax_has_mtime = false;
    char *g_path = NULL;                          /* pax 'g' global is not carried; freed if seen (rare) */
    (void)g_path;
    fulta_arc_err_t e = FULTA_ARC_OK;
    uint64_t pos = 0;
    int zero_run = 0;

    for (;;) {
        if (pos + BLK > src->size) break;                /* no room for another header: end */
        if (fa_source_read_exact(src, pos, h, BLK)) { e = FULTA_ARC_ERR_TRUNCATED; break; }
        if (tar_all_zero(h)) { if (++zero_run >= 2) break; pos += BLK; continue; }
        zero_run = 0;
        if (!tar_checksum_ok(h)) break;                  /* a non-zero, non-checksumming block ends the archive */

        uint64_t hsize;
        if (!tar_num(h + 124, 12, &hsize)) { e = FULTA_ARC_ERR_CORRUPT; break; }
        uint8_t type = h[156];
        uint64_t data = pos + BLK;

        /* meta blocks (GNU L/K long name/link, pax x/g): their own data is `hsize` bytes (capped) and must fit */
        if (type == 'L' || type == 'K' || type == 'x' || type == 'g') {
            if (hsize > NAME_MAX_META) { e = FULTA_ARC_ERR_CORRUPT; break; }
            uint64_t pad = (hsize + 511) & ~(uint64_t)511;
            if (data > src->size || pad > src->size - data) { e = FULTA_ARC_ERR_TRUNCATED; break; }
            uint8_t *buf = fa_malloc((size_t)hsize + 1);
            if (!buf) { e = FULTA_ARC_ERR_NOMEM; break; }
            if (fa_source_read_exact(src, data, buf, (size_t)hsize)) { fa_free(buf); e = FULTA_ARC_ERR_TRUNCATED; break; }
            buf[hsize] = 0;
            if (type == 'L') { fa_free(long_name); long_name = (char *)buf; }
            else if (type == 'K') { fa_free(long_link); long_link = (char *)buf; }
            else {                                       /* pax: path/linkpath/size/mtime override the next member */
                char *p = pax_get(buf, (size_t)hsize, "path");
                if (p) { fa_free(pax_path); pax_path = p; }
                char *l = pax_get(buf, (size_t)hsize, "linkpath");
                if (l) { fa_free(pax_link); pax_link = l; }
                char *s = pax_get(buf, (size_t)hsize, "size");
                if (s) { int64_t v = tar_dec(s); pax_size = v < 0 ? 0 : (uint64_t)v; pax_has_size = true; fa_free(s); }
                char *m = pax_get(buf, (size_t)hsize, "mtime");
                if (m) { pax_mtime = tar_dec(m); pax_has_mtime = true; fa_free(m); }
                fa_free(buf);
            }
            pos = data + pad;
            continue;
        }

        /* an ordinary member: the real byte count is the pax `size` when present, else the header octal (pax-made
         * ≥8 GiB files leave the header field 0 and carry the size only in the extended record). */
        uint64_t eff = pax_has_size ? pax_size : hsize;
        uint64_t pad = (eff > UINT64_MAX - 511) ? 0 : ((eff + 511) & ~(uint64_t)511);
        if (pad == 0 && eff != 0) { e = FULTA_ARC_ERR_CORRUPT; break; }
        if (data > src->size || pad > src->size - data) {   /* a truncated trailing file: list what is there */
            pad = data <= src->size ? src->size - data : 0;
            if (eff > pad) eff = pad;
        }

        /* assemble its name (pax path > GNU longname > prefix+name) */
        char namebuf[257];                               /* prefix(155) + '/' + name(100) + NUL */
        const char *name_src;
        size_t name_len;
        if (pax_path) { name_src = pax_path; name_len = strlen(pax_path); }
        else if (long_name) { name_src = long_name; name_len = strlen(long_name); }
        else {
            size_t nl = 0; while (nl < 100 && h[nl]) nl++;
            size_t pl = 0; while (pl < 155 && h[345 + pl]) pl++;
            if (pl) { memcpy(namebuf, h + 345, pl); namebuf[pl] = '/'; memcpy(namebuf + pl + 1, h, nl); name_len = pl + 1 + nl; }
            else { memcpy(namebuf, h, nl); name_len = nl; }
            namebuf[name_len] = 0;
            name_src = namebuf;
        }

        /* a safe UTF-8 name copy for the public field */
        char *safe = fa_malloc(name_len + 1);
        if (!safe) { e = FULTA_ARC_ERR_NOMEM; break; }
        memcpy(safe, name_src, name_len);
        safe[name_len] = 0;
        bool is_dir = (type == '5') || (name_len && safe[name_len - 1] == '/');
        if (!fa_sanitize_name(safe)) { fa_free(safe); safe = NULL; }   /* unsafe (e.g. "..") -> the "data" placeholder */
        const char *use = safe && safe[0] ? safe : "data";

        uint64_t esize = (type == '0' || type == 0 || type == '7') ? eff : 0;
        char linkbuf[101];
        const char *link = NULL;
        if (type == '2' || type == '1') {               /* symlink / hardlink target */
            if (pax_link) link = pax_link;
            else if (long_link) link = long_link;        /* GNU 'K' (target > 100 chars) */
            else {                                       /* the common case: in the header's linkname field */
                size_t ll = 0; while (ll < 100 && h[157 + ll]) ll++;
                memcpy(linkbuf, h + 157, ll); linkbuf[ll] = 0;
                link = linkbuf;
            }
        }

        fa_entry_t *en = fa_add_entry(arc, (const uint8_t *)name_src, name_len, use, "store");
        tar_item_t *it = tar_add_item(st);
        fa_free(safe);
        if (!en || !it) { e = FULTA_ARC_ERR_NOMEM; break; }

        if (is_dir) {
            en->pub.flags |= FULTA_ARC_ENTRY_DIR;
        } else if (type == '2') {
            en->pub.flags |= FULTA_ARC_ENTRY_SYMLINK;
            if (link) { it->link = fa_strndup(link, strlen(link)); esize = it->link ? strlen(it->link) : 0; }
        } else if (type == 'S') {
            en->pub.flags |= FULTA_ARC_ENTRY_UNSUPPORTED;   /* GNU sparse: layout needs the sparse map */
        }
        en->pub.size = esize;
        en->pub.packed_size = esize;
        if (!it->link) { it->off = data; it->size = esize; }   /* esize already reflects the pax size */

        uint64_t mode, mtime;
        if (tar_num(h + 100, 8, &mode) && (mode & 07777)) { en->pub.unix_mode = (uint32_t)(mode & 0xFFFFF); }
        if (pax_has_mtime) { en->pub.mtime = pax_mtime * INT64_C(1000000000); en->pub.flags |= FULTA_ARC_ENTRY_HAS_MTIME; }
        else if (tar_num(h + 136, 12, &mtime)) { en->pub.mtime = (int64_t)mtime * INT64_C(1000000000); en->pub.flags |= FULTA_ARC_ENTRY_HAS_MTIME; }

        /* the overrides are one-shot (pax 'x', GNU L/K) */
        fa_free(long_name); long_name = NULL;
        fa_free(long_link); long_link = NULL;
        fa_free(pax_path); pax_path = NULL;
        fa_free(pax_link); pax_link = NULL;
        pax_has_size = false;
        pax_has_mtime = false;
        pos = data + pad;
    }

    fa_free(long_name);
    fa_free(long_link);
    fa_free(pax_path);
    fa_free(pax_link);
    if (e) { tar_close(arc); return e; }
    if (arc->count == 0) { tar_close(arc); return FULTA_ARC_ERR_NOT_ARCHIVE; }   /* a lone header, nothing listed */
    return FULTA_ARC_OK;
}

static fulta_arc_err_t tar_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    tar_state_t *st = arc->state;
    if (index >= st->n) return FULTA_ARC_ERR_INVALID_ARG;
    tar_item_t *it = &st->items[index];
    if (it->link) return sink->write(sink->ctx, it->link, strlen(it->link));   /* symlink target as the data */
    if (it->size == 0) return FULTA_ARC_OK;

    const fulta_arc_source_t *src = arc->volumes[0];
    fa_piece_t piece = { src, it->off, it->size };
    fa_range_t range = { &piece, 1, it->size };
    fa_stream_t *s = NULL;
    fulta_arc_err_t e = fa_stream_range(&range, 0, it->size, &s);
    if (e) return e;
    e = fa_pump(s, it->size, false, 0, sink, arc, arc->opt.keep_damaged);
    fa_stream_destroy(s);
    return e;
}

static void tar_close(fulta_arc_t *arc) {
    tar_state_t *st = arc->state;
    if (!st) return;
    for (size_t i = 0; i < st->n; i++) fa_free(st->items[i].link);
    fa_free(st->items);
    fa_free(st);
    arc->state = NULL;
}

const fa_format_ops_t fa_tar_ops = {
    .format = FULTA_ARC_FORMAT_TAR,
    .probe = NULL,                  /* no signature in the first 64 bytes (ustar magic is at offset 257); detected by
                                       tar_open validating the first block's checksum in the fallback scan */
    .open = tar_open,
    .extract = tar_extract,
    .close = tar_close,
};
