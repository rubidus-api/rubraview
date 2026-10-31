/* cpio.c - FultaArc: cpio reader - SVR4 "newc"/"crc", POSIX "odc", and old-binary. MIT.
 * Original FultaArc code from the public cpio formats (POSIX cpio + the documented SVR4 and old-binary layouts,
 * BSD cpio(5)); no GPL/LGPL source read or copied. A cpio archive is a flat sequence of members (header + name +
 * data) ending at a member named "TRAILER!!!". This reader lists each member and streams its stored bytes (cpio is
 * uncompressed; no encryption). No writer. See D-020. */
#include "../core/internal.h"

#define CPIO_NAME_MAX (64u * 1024u)   /* cap a member name before allocating (untrusted input) */
#define CPIO_TRAILER "TRAILER!!!"

typedef enum { CPIO_NEWC, CPIO_CRC, CPIO_ODC, CPIO_BIN_LE, CPIO_BIN_BE } cpio_fmt_t;

typedef struct cpio_item { uint64_t off, size; } cpio_item_t;

typedef struct cpio_state {
    cpio_item_t *items;
    size_t n, cap;
    cpio_fmt_t fmt;
} cpio_state_t;

/* Parse `len` ASCII digits in base 8 or 16 at `p`; false on a bad digit. */
static bool cpio_num(const uint8_t *p, size_t len, int base, uint64_t *out) {
    uint64_t v = 0;
    for (size_t i = 0; i < len; i++) {
        int c = p[i], d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        if (d >= base) return false;
        v = v * (uint64_t)base + (uint64_t)d;
    }
    *out = v;
    return true;
}

static uint32_t cpio_u16(const uint8_t *p, bool be) { return be ? (uint32_t)(p[0] << 8 | p[1]) : (uint32_t)(p[1] << 8 | p[0]); }

static cpio_item_t *cpio_add(cpio_state_t *st) {
    if (st->n == st->cap) {
        size_t nc = st->cap ? st->cap * 2 : 32;
        cpio_item_t *q = fa_realloc(st->items, nc * sizeof *q);
        if (!q) return NULL;
        st->items = q;
        st->cap = nc;
    }
    cpio_item_t *it = &st->items[st->n++];
    *it = (cpio_item_t){0};
    return it;
}

static void cpio_close(fulta_arc_t *arc);

/* S_IF* type bits (octal, portable constants). */
#define CP_IFMT 0170000u
#define CP_IFREG 0100000u
#define CP_IFDIR 0040000u
#define CP_IFLNK 0120000u

static fulta_arc_err_t cpio_open(fulta_arc_t *arc, uint64_t offset) {
    (void)offset;
    const fulta_arc_source_t *src = arc->volumes[0];
    uint8_t m[6];
    if (src->size < 6 || fa_source_read_exact(src, 0, m, 6)) return FULTA_ARC_ERR_NOT_ARCHIVE;

    cpio_fmt_t fmt;
    size_t hdr;
    if (memcmp(m, "070701", 6) == 0) { fmt = CPIO_NEWC; hdr = 110; }
    else if (memcmp(m, "070702", 6) == 0) { fmt = CPIO_CRC; hdr = 110; }
    else if (memcmp(m, "070707", 6) == 0) { fmt = CPIO_ODC; hdr = 76; }
    else if (m[0] == 0xC7 && m[1] == 0x71) { fmt = CPIO_BIN_LE; hdr = 26; }
    else if (m[0] == 0x71 && m[1] == 0xC7) { fmt = CPIO_BIN_BE; hdr = 26; }
    else return FULTA_ARC_ERR_NOT_ARCHIVE;

    cpio_state_t *st = fa_calloc(1, sizeof *st);
    if (!st) return FULTA_ARC_ERR_NOMEM;
    st->fmt = fmt;
    arc->state = st;

    fulta_arc_err_t e = FULTA_ARC_OK;
    uint64_t pos = 0;
    for (;;) {
        if (pos + hdr > src->size) break;
        uint8_t h[110];
        if (fa_source_read_exact(src, pos, h, hdr)) break;

        uint64_t namesize, filesize, mode;
        if (fmt == CPIO_NEWC || fmt == CPIO_CRC) {
            if (!cpio_num(h + 14, 8, 16, &mode) || !cpio_num(h + 54, 8, 16, &filesize) ||
                !cpio_num(h + 94, 8, 16, &namesize)) break;
        } else if (fmt == CPIO_ODC) {
            if (!cpio_num(h + 18, 6, 8, &mode) || !cpio_num(h + 59, 6, 8, &namesize) ||
                !cpio_num(h + 65, 11, 8, &filesize)) break;
        } else {
            bool be = fmt == CPIO_BIN_BE;
            mode = cpio_u16(h + 6, be);
            namesize = cpio_u16(h + 20, be);
            filesize = ((uint64_t)cpio_u16(h + 22, be) << 16) | cpio_u16(h + 24, be);   /* high word first */
        }
        if (namesize == 0 || namesize > CPIO_NAME_MAX) break;

        uint64_t name_off = pos + hdr;
        if (namesize > src->size - name_off) break;
        /* name padding: newc/crc pad (header+name) to 4; old-binary pads name to 2; odc has none */
        uint64_t data_off = name_off + namesize;
        if (fmt == CPIO_NEWC || fmt == CPIO_CRC) data_off += (4 - ((hdr + namesize) & 3)) & 3;
        else if (fmt == CPIO_BIN_LE || fmt == CPIO_BIN_BE) data_off += namesize & 1;

        char *name = fa_malloc((size_t)namesize + 1);
        if (!name) { e = FULTA_ARC_ERR_NOMEM; break; }
        if (fa_source_read_exact(src, name_off, name, (size_t)namesize)) { fa_free(name); break; }
        name[namesize] = 0;
        size_t nlen = 0;
        while (nlen < namesize && name[nlen]) nlen++;   /* the stored name includes a NUL */

        if (nlen == strlen(CPIO_TRAILER) && memcmp(name, CPIO_TRAILER, nlen) == 0) { fa_free(name); break; }

        if (data_off > src->size || filesize > src->size - data_off) { fa_free(name); break; }
        uint64_t advance = data_off + filesize;
        if (fmt == CPIO_NEWC || fmt == CPIO_CRC) advance += (4 - (filesize & 3)) & 3;
        else if (fmt == CPIO_BIN_LE || fmt == CPIO_BIN_BE) advance += filesize & 1;

        char *safe = fa_malloc(nlen + 1);
        if (!safe) { fa_free(name); e = FULTA_ARC_ERR_NOMEM; break; }
        memcpy(safe, name, nlen);
        safe[nlen] = 0;
        bool is_dir = (mode & CP_IFMT) == CP_IFDIR;
        bool is_lnk = (mode & CP_IFMT) == CP_IFLNK;
        if (!fa_sanitize_name(safe)) { fa_free(safe); safe = NULL; }
        const char *use = safe && safe[0] ? safe : "data";

        fa_entry_t *en = fa_add_entry(arc, (const uint8_t *)name, nlen, use, "store");
        cpio_item_t *it = cpio_add(st);
        fa_free(safe);
        fa_free(name);
        if (!en || !it) { e = FULTA_ARC_ERR_NOMEM; break; }

        if (is_dir) {
            en->pub.flags |= FULTA_ARC_ENTRY_DIR;
        } else {
            /* a symlink's target is its file data (filesize bytes), so it streams like a regular file - no caching,
             * no strlen (the target may contain a NUL). Only the flag differs. */
            if (is_lnk) en->pub.flags |= FULTA_ARC_ENTRY_SYMLINK;
            en->pub.size = filesize;
            en->pub.packed_size = filesize;
            it->off = data_off;
            it->size = filesize;
        }
        if (mode & 07777) en->pub.unix_mode = (uint32_t)(mode & 0xFFFFF);
        pos = advance;
    }

    if (e) { cpio_close(arc); return e; }   /* only NOMEM */
    if (arc->count == 0) { cpio_close(arc); return FULTA_ARC_ERR_NOT_ARCHIVE; }
    return FULTA_ARC_OK;
}

static fulta_arc_err_t cpio_extract(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    cpio_state_t *st = arc->state;
    if (index >= st->n) return FULTA_ARC_ERR_INVALID_ARG;
    cpio_item_t *it = &st->items[index];
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

static void cpio_close(fulta_arc_t *arc) {
    cpio_state_t *st = arc->state;
    if (!st) return;
    fa_free(st->items);
    fa_free(st);
    arc->state = NULL;
}

static bool cpio_probe(const uint8_t *head, size_t n, uint64_t *offset) {
    if (n < 6) return false;
    if (memcmp(head, "070701", 6) == 0 || memcmp(head, "070702", 6) == 0 || memcmp(head, "070707", 6) == 0 ||
        (head[0] == 0xC7 && head[1] == 0x71) || (head[0] == 0x71 && head[1] == 0xC7)) {
        *offset = 0;
        return true;
    }
    return false;
}

const fa_format_ops_t fa_cpio_ops = {
    .format = FULTA_ARC_FORMAT_CPIO,
    .probe = cpio_probe,
    .open = cpio_open,
    .extract = cpio_extract,
    .close = cpio_close,
};
