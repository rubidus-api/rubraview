/* lha.c - FultaArc: LHA/LZH archive reader (.lzh, .lha; LArc .lzs, PMarc .pma; self-extractors). MIT.
 *
 * Glue only. The decoding is lhadec in src/lha/lhadec/: the deliverable of the LHA clean room
 * (fultaarc-cleanroom-lha, closed 2026-10-16 at 5792dae, log L0001-L0033, audits 1-8), copied byte-identical to the
 * room's close-out MANIFEST (re-checked by tests/tools/lhadec-pin.sh) and compiled through lhadec_shim.h (allocator
 * seam and fa_lha_* symbol names; no source edit). The room wrote lhadec from its own specification (lzh-container.md,
 * lzh-methods.md), which it derived from Haruhiko Okumura's public-domain ar002 and from black-box runs of lhasa 0.6.0
 * (ISC), UNLHA32.DLL 3.00 (-lh2-/-lh3-) and PMarc 2.00/PMext 2.22 (-pm2-) on its corpus and on input of its own
 * making. Neither that room nor this file read the source of any LHA reader (Unix lha, lhasa, UNLHA32, 7-Zip,
 * libarchive, XADMaster). Format, algorithm, derivation and provenance: docs/provenance/lha/.
 *
 * lhadec reads sequentially and cannot seek, so open() lists every entry by reading the archive through once, and
 * extract(i) opens a fresh lhadec reader at entry i's header offset. FultaArc's reader policies here (choices, not
 * format facts; lha.md lists each):
 *  - detection at offset 0 uses the room's own plausibility rule (method string, level 0-3, header sum for levels
 *    0/1 when the header fits the 64-byte head); open() then refuses (NOT_ARCHIVE) a source whose first entry does
 *    not parse, so a stray "-lh5-" pattern cannot claim a file as a damaged LHA;
 *  - a self-extractor is searched for (the room's 1 MiB scan, container §6) only behind a stub of one of the six
 *    kinds the corpus' 15 self-extractors carry, and LHA is the last format tried;
 *  - names are path + name as stored, decoded as UTF-8 when valid, else through the code page (CP932 when AUTO:
 *    LHA's home is Japan; the format itself names no encoding), after undoing lhadec's split of a double-byte
 *    character whose second byte is 0x5C ('\') in CP932/CP936/CP950 (lha_restore_dbcs_5c);
 *  - mtime: Windows FILETIME (UTC) when present, else the Unix time (UTC), else the DOS date-time (local time);
 *  - the data check is lhadec's CRC-16 (no CRC-32 is stored), reported as FULTA_ARC_ERR_CHECKSUM;
 *  - a symbolic link's data is its target (as for tar); a directory without a name is not listed.
 * Decode only: FultaArc never writes LHA. */
#include "../core/internal.h"

#include "lhadec_rename.h"
#include "lhadec/include/lhadec.h"

#define LHA_SFX_SCAN ((size_t)1 << 20)   /* the room's self-extractor scan window (container §6) */

typedef struct lha_item {
    uint64_t hdr_off;      /* the entry's header offset in the source */
    uint64_t size;         /* original size */
    uint16_t crc;          /* stored CRC-16, re-checked against the header extract() parses */
    lha_method method;
    char *link;            /* symbolic-link target (NUL-terminated), or NULL */
} lha_item_t;

typedef struct lha_state {
    lha_item_t *items;
    size_t n, cap;
} lha_state_t;

/* lhadec byte source over a FultaArc source, starting at `pos`. Errors are kept here so the caller can return the
 * real cause (an I/O error, a cancellation) rather than lhadec's generic LHA_ERR_IO. */
typedef struct lha_rd {
    const fulta_arc_source_t *src;
    uint64_t pos;
    fulta_arc_t *arc;
    fulta_arc_err_t err;
} lha_rd_t;

static size_t lha_read_cb(void *ctx, void *buf, size_t n) {
    lha_rd_t *r = ctx;
    if (fa_cancelled(r->arc)) { r->err = FULTA_ARC_ERR_CANCELLED; return (size_t)-1; }
    if (r->pos >= r->src->size) return 0;
    if (n > r->src->size - r->pos) n = (size_t)(r->src->size - r->pos);
    size_t got = 0;
    fulta_arc_err_t e = r->src->read_at(r->src->ctx, r->pos, buf, n, &got);
    if (e) { r->err = e; return (size_t)-1; }
    r->pos += got;
    return got;
}

/* lhadec byte sink over a FultaArc sink; a sink error or a cancellation is kept and stops lhadec. */
typedef struct lha_wr {
    const fulta_arc_sink_t *sink;
    fulta_arc_t *arc;
    fulta_arc_err_t err;
} lha_wr_t;

static int lha_write_cb(void *ctx, const void *buf, size_t n) {
    lha_wr_t *w = ctx;
    if (fa_cancelled(w->arc)) { w->err = FULTA_ARC_ERR_CANCELLED; return 1; }
    fa_progress(w->arc, n);
    if (w->sink && w->sink->write) {
        fulta_arc_err_t e = w->sink->write(w->sink->ctx, buf, n);
        if (e) { w->err = e; return 1; }
    }
    return 0;
}

static fulta_arc_err_t lha_map(int rc) {
    switch (rc) {
    case LHA_OK: return FULTA_ARC_OK;
    case LHA_ERR_IO: return FULTA_ARC_ERR_IO;
    case LHA_ERR_FORMAT:
    case LHA_ERR_HEADER_SUM:
    case LHA_ERR_HEADER_CRC:
    case LHA_ERR_DATA: return FULTA_ARC_ERR_CORRUPT;
    case LHA_ERR_UNSUPPORTED: return FULTA_ARC_ERR_UNSUPPORTED;
    case LHA_ERR_TRUNCATED: return FULTA_ARC_ERR_TRUNCATED;
    case LHA_ERR_CRC: return FULTA_ARC_ERR_CHECKSUM;
    case LHA_ERR_NOMEM: return FULTA_ARC_ERR_NOMEM;
    case LHA_ERR_STATE: return FULTA_ARC_ERR_INVALID_ARG;   /* glue misuse of lhadec's API */
    case LHA_ERR_NOT_FOUND: return FULTA_ARC_ERR_NOT_ARCHIVE;
    default: return FULTA_ARC_ERR_CORRUPT;                  /* LHA_END where an entry was expected */
    }
}

/* The room's plausibility rule for an entry start (lhadec's looks_like_header, container §3/§6), on the head. */
static bool lha_header_plausible(const uint8_t *b, size_t n) {
    if (n < 22 || b[2] != '-' || b[6] != '-') return false;
    if (!((b[3] == 'l' && (b[4] == 'h' || b[4] == 'z' || b[4] == 'k')) || (b[3] == 'p' && b[4] == 'm'))) return false;
    if (b[20] > 3) return false;
    if (b[20] <= 1) {                    /* levels 0/1: header size >= 22 and the byte sum, when it fits the head */
        size_t S = b[0];
        if (S < 22) return false;
        if (S + 2 <= n) {
            unsigned s = 0;
            for (size_t i = 0; i < S; i++) s += b[2 + i];
            if ((s & 0xFF) != b[1]) return false;
        }
    }
    return true;
}

/* The stubs of the corpus' 15 self-extractors (lhasa 0.6.0 test/archives), six kinds: DOS/Windows "MZ" (9), DOS .COM
 * opening with a jump 0xE9/0xEB (2), Amiga hunk file, HUNK_HEADER 0x000003F3 (1), Atari TOS program 0x601A (1), Sharp
 * X68000 ".x" "HU" (1), and PMARC2's MSX-DOS .COM, which opens with a header-shaped "-pms-" record whose level byte is
 * out of range (1; seen in pmarc2/sfx.com). Used only to decide whether to search at all. */
static bool lha_exe_stub(const uint8_t *h, size_t n) {
    if (n < 7) return false;
    if (h[0] == 'M' && h[1] == 'Z') return true;
    if (h[0] == 0x00 && h[1] == 0x00 && h[2] == 0x03 && h[3] == 0xF3) return true;
    if (h[0] == 0x60 && h[1] == 0x1A) return true;
    if (h[0] == 'H' && h[1] == 'U') return true;
    if (h[0] == 0xE9 || h[0] == 0xEB) return true;
    if (memcmp(h + 2, "-pms-", 5) == 0) return true;
    return false;
}

static bool lha_probe(const uint8_t *head, size_t n, uint64_t *offset) {
    if (!lha_header_plausible(head, n)) return false;
    *offset = 0;
    return true;
}

static lha_item_t *lha_add_item(lha_state_t *st) {
    if (st->n == st->cap) {
        size_t nc = st->cap ? st->cap * 2 : 16;
        lha_item_t *p = fa_realloc(st->items, nc * sizeof *p);
        if (!p) return NULL;
        st->items = p;
        st->cap = nc;
    }
    lha_item_t *it = &st->items[st->n++];
    memset(it, 0, sizeof *it);
    return it;
}

static bool lha_unsupported(lha_method m) { return m == LHA_M_UNKNOWN; }   /* lhadec decodes every named method */

/* FultaArc's short method name: the method string without its dashes ("lh5"; "lk7" when lhadec selected the LHARK
 * decoding for "-lh7-", container §6), unprintable bytes of an unknown string shown as '?'. */
static void lha_method_text(const lha_entry *le, char out[8]) {
    const char *id = le->method == LHA_M_UNKNOWN ? le->method_id : lha_method_name(le->method);
    size_t len = strlen(id), k = 0;
    size_t from = (len == 5 && id[0] == '-' && id[4] == '-') ? 1 : 0, to = from ? 4 : len;
    for (size_t i = from; i < to && k < 7; i++) {
        unsigned char c = (unsigned char)id[i];
        out[k++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    out[k] = 0;
    if (!k) memcpy(out, "?", 2);
}

/* FultaArc's double-byte code-page tables (src/text/cp_tables.c): lead[c - 0x80] != 0 when byte c starts a pair. */
extern const uint8_t fa_cp932_lead[128], fa_cp936_lead[128], fa_cp950_lead[128];

/* lhadec reads both '\' and '/' in a level-0/1 name as separators and returns '/' for each (container §3.3). In
 * Shift-JIS (CP932), GBK (CP936) and Big5 (CP950) 0x5C is also a valid second byte (CP932 表 = 95 5C, ソ = 83 5C), so
 * such a character arrives split, as its first byte and a '/'. FultaArc's decoder takes a second byte only from
 * 0x40-0xFE (src/text/codepage.c), so a '/' (0x2F) right after a first byte cannot be a second byte as stored: it is a
 * 0x5C that lhadec turned into '/'. Put it back, scanning in pairs, when - and only when - the name is decoded through
 * one of those three code pages, chosen exactly as fa_decode_name chooses (not UTF-8; options.codepage, else
 * options.auto_codepage, else CP932). Found by a synthetic archive (tests/test_lha.c); the corpus has no such name. */
static void lha_restore_dbcs_5c(const fulta_arc_t *arc, uint8_t *s, size_t n) {
    if (fa_utf8_valid(s, n)) return;
    fulta_arc_codepage_t cp = arc->opt.codepage;
    if (cp == FULTA_ARC_CP_AUTO) cp = arc->opt.auto_codepage ? arc->opt.auto_codepage : FULTA_ARC_CP_932;
    const uint8_t *lead = cp == FULTA_ARC_CP_932 ? fa_cp932_lead : cp == FULTA_ARC_CP_936 ? fa_cp936_lead
                        : cp == FULTA_ARC_CP_950 ? fa_cp950_lead : NULL;
    if (!lead) return;
    for (size_t i = 0; i + 1 < n; i++) {
        if (s[i] < 0x80 || !lead[s[i] - 0x80]) continue;
        if (s[i + 1] == '/') s[i + 1] = 0x5C;
        i++;                                     /* the second byte of the pair */
    }
}

/* Register one lhadec entry. A directory whose name is empty after sanitising is not listed (nothing to create).
 * A symbolic link is stored as a "-lhd-" entry whose name holds "name|target" (container §4.5), so lhadec reports it
 * with both LHA_IS_DIR and LHA_HAS_SYMLINK: the link wins. */
static fulta_arc_err_t lha_emit(fulta_arc_t *arc, lha_state_t *st, const lha_entry *le) {
    bool link = (le->flags & LHA_HAS_SYMLINK) != 0;
    bool dir = !link && (le->flags & LHA_IS_DIR) != 0;
    size_t rn = le->path_len + le->name_len;
    uint8_t *raw = fa_malloc(rn + 1);
    if (!raw) return FULTA_ARC_ERR_NOMEM;
    if (le->path_len) memcpy(raw, le->path, le->path_len);
    if (le->name_len) memcpy(raw + le->path_len, le->name, le->name_len);
    raw[rn] = 0;
    lha_restore_dbcs_5c(arc, raw, rn);
    size_t un = rn;
    while (un && raw[un - 1] == '/') un--;              /* a directory's path ends in '/' */

    bool not_unicode = false;
    char *name = fa_decode_name(arc, raw, un, false, FULTA_ARC_CP_932, &not_unicode);
    if (!name) { fa_free(raw); return FULTA_ARC_ERR_NOMEM; }
    bool named = fa_sanitize_name(name) && name[0];
    if (!named && dir) { fa_free(name); fa_free(raw); return FULTA_ARC_OK; }

    char method[8];
    lha_method_text(le, method);
    fa_entry_t *en = fa_add_entry(arc, raw, rn, named ? name : "data", method);
    lha_item_t *it = en ? lha_add_item(st) : NULL;
    fa_free(name);
    fa_free(raw);
    if (!en || !it) return FULTA_ARC_ERR_NOMEM;

    it->hdr_off = le->header_offset;
    it->size = le->original_size;
    it->crc = le->crc;
    it->method = le->method;
    en->pub.packed_size = le->packed_size;
    if (dir) {
        en->pub.flags |= FULTA_ARC_ENTRY_DIR;
        en->pub.size = 0;
    } else if (link) {
        en->pub.flags |= FULTA_ARC_ENTRY_SYMLINK;
        it->link = fa_strndup((const char *)le->link, le->link_len);
        if (!it->link) return FULTA_ARC_ERR_NOMEM;
        en->pub.size = strlen(it->link);
    } else {
        en->pub.size = le->original_size;
        if (lha_unsupported(le->method)) en->pub.flags |= FULTA_ARC_ENTRY_UNSUPPORTED;
    }
    if (not_unicode) en->pub.flags |= FULTA_ARC_ENTRY_NAME_NOT_UNICODE;

    if ((le->flags & LHA_HAS_WIN_TIMES) && le->win_mtime) {
        en->pub.mtime = fa_filetime_ns(le->win_mtime);
        en->pub.flags |= FULTA_ARC_ENTRY_HAS_MTIME;
    } else if (le->flags & LHA_HAS_UNIX_TIME) {
        en->pub.mtime = (int64_t)le->unix_time * INT64_C(1000000000);
        en->pub.flags |= FULTA_ARC_ENTRY_HAS_MTIME;
    } else if (le->flags & LHA_HAS_DOS_TIME) {   /* (date << 16) | time, local time (container §2) */
        en->pub.mtime = fa_dos_time_ns((uint16_t)(le->dos_time >> 16), (uint16_t)(le->dos_time & 0xFFFF));
        en->pub.flags |= FULTA_ARC_ENTRY_HAS_MTIME;
    }
    if ((le->flags & LHA_HAS_UNIX_PERM) && (le->unix_perm & 07777)) en->pub.unix_mode = le->unix_perm;
    return FULTA_ARC_OK;
}

static void lha_close_fmt(fulta_arc_t *arc);

static fulta_arc_err_t lha_open_fmt(fulta_arc_t *arc, uint64_t offset) {
    const fulta_arc_source_t *src = arc->volumes[0];
    size_t scan = 0;
    if (offset == UINT64_MAX) {                  /* second pass: search only behind an executable stub */
        uint8_t h[8];
        if (src->size < 26 || fa_source_read_exact(src, 0, h, sizeof h) || !lha_exe_stub(h, sizeof h))
            return FULTA_ARC_ERR_NOT_ARCHIVE;
        scan = LHA_SFX_SCAN;
    }

    lha_state_t *st = fa_calloc(1, sizeof *st);
    if (!st) return FULTA_ARC_ERR_NOMEM;
    arc->state = st;

    lha_rd_t rd = { .src = src, .pos = 0, .arc = arc, .err = FULTA_ARC_OK };
    lha_reader *r = NULL;
    int rc = lha_open(&r, lha_read_cb, &rd, scan);
    fulta_arc_err_t e = FULTA_ARC_OK;
    if (rc) {
        e = rd.err ? rd.err : rc == LHA_ERR_NOT_FOUND ? FULTA_ARC_ERR_NOT_ARCHIVE : lha_map(rc);
    } else {
        /* A header that does not parse ends the listing and keeps the entries before it (as the ar reader does);
         * the first one failing means this is not an LHA archive. A source error or a cancellation is returned. */
        for (;;) {
            lha_entry le;
            rc = lha_next(r, &le);
            if (rc == LHA_END) break;
            if (rc) {
                if (rd.err) e = rd.err;
                else if (rc == LHA_ERR_NOMEM) e = FULTA_ARC_ERR_NOMEM;
                else if (st->n == 0) e = FULTA_ARC_ERR_NOT_ARCHIVE;
                break;
            }
            if ((e = lha_emit(arc, st, &le))) break;
        }
        lha_close(r);
        if (!e && scan && st->n == 0) e = FULTA_ARC_ERR_NOT_ARCHIVE;   /* a stub alone is not an LHA archive */
    }
    if (e) { lha_close_fmt(arc); return e; }
    return FULTA_ARC_OK;
}

static fulta_arc_err_t lha_extract_fmt(fulta_arc_t *arc, size_t index, const fulta_arc_sink_t *sink) {
    lha_state_t *st = arc->state;
    if (index >= st->n) return FULTA_ARC_ERR_INVALID_ARG;
    const lha_item_t *it = &st->items[index];
    uint32_t flags = arc->entries[index].pub.flags;
    if (flags & FULTA_ARC_ENTRY_DIR) return FULTA_ARC_OK;
    if (it->link) {                              /* the link target is the data */
        size_t n = strlen(it->link);
        fa_progress(arc, n);
        return sink && sink->write ? sink->write(sink->ctx, it->link, n) : FULTA_ARC_OK;
    }
    if (flags & FULTA_ARC_ENTRY_UNSUPPORTED) return FULTA_ARC_ERR_UNSUPPORTED;

    lha_rd_t rd = { .src = arc->volumes[0], .pos = it->hdr_off, .arc = arc, .err = FULTA_ARC_OK };
    lha_reader *r = NULL;
    int rc = lha_open(&r, lha_read_cb, &rd, 0);
    if (rc) return rd.err ? rd.err : lha_map(rc);
    lha_entry le;
    rc = lha_next(r, &le);
    if (rc == LHA_OK && (le.original_size != it->size || le.crc != it->crc || le.method != it->method))
        rc = LHA_ERR_FORMAT;                     /* not the entry listed at this offset: the source changed */
    if (rc != LHA_OK) {
        lha_close(r);
        return rd.err ? rd.err : lha_map(rc);
    }
    lha_wr_t wr = { .sink = sink, .arc = arc, .err = FULTA_ARC_OK };
    rc = lha_extract(r, lha_write_cb, &wr);
    lha_close(r);
    if (wr.err) return wr.err;                   /* the sink's own failure, or a cancellation: never "damaged" */
    if (rd.err) return rd.err;
    fulta_arc_err_t e = lha_map(rc);
    /* keep what decoded (fa_pump's contract): a decode-side failure becomes FULTA_ARC_DAMAGED */
    if (e && arc->opt.keep_damaged && e != FULTA_ARC_ERR_NOMEM && e != FULTA_ARC_ERR_UNSUPPORTED) return FULTA_ARC_DAMAGED;
    return e;
}

static void lha_close_fmt(fulta_arc_t *arc) {
    lha_state_t *st = arc->state;
    if (!st) return;
    for (size_t i = 0; i < st->n; i++) fa_free(st->items[i].link);
    fa_free(st->items);
    fa_free(st);
    arc->state = NULL;
}

const fa_format_ops_t fa_lha_ops = {
    .format = FULTA_ARC_FORMAT_LHA,
    .probe = lha_probe,      /* an LHA entry at offset 0; self-extractors in open(UINT64_MAX) */
    .open = lha_open_fmt,
    .extract = lha_extract_fmt,
    .close = lha_close_fmt,
};
