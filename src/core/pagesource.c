#include "rubraview/pagesource.h"
#include "rubraview/glob.h"
#include "rubraview/path.h"
#include "rubraview/utf8.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define ARCHIVE_FILTER "*.cbz;*.zip;*.cb7;*.7z;*.cbr;*.rar"
#define COMICINFO_NAME "ComicInfo.xml"

static bool name_is_comicinfo(u8str_t name) {
    u8str_t base = rubraview_path_basename(name);
    if (base.len != sizeof(COMICINFO_NAME) - 1) return false;
    /* The manifest sits in the archive root but archivers differ on
       case, so match it case-insensitively. */
    for (size_t i = 0; i < base.len; ++i) {
        char a = base.ptr[i], b = COMICINFO_NAME[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

typedef struct ref_buf {
    rubraview_page_ref_t *data;
    size_t count, capacity;
} ref_buf_t;

static bool ref_buf_push(proven_arena_t *arena, ref_buf_t *b, rubraview_page_ref_t ref) {
    if (b->count >= b->capacity) {
        size_t new_cap = b->capacity == 0 ? 32 : b->capacity * 2;
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, new_cap, sizeof(rubraview_page_ref_t));
        if (!proven_is_ok(res.err)) return false;
        rubraview_page_ref_t *data = (rubraview_page_ref_t*)(void*)res.value.ptr;
        if (b->data && b->count > 0) memcpy(data, b->data, b->count * sizeof(rubraview_page_ref_t));
        b->data = data;
        b->capacity = new_cap;
    }
    b->data[b->count++] = ref;
    return true;
}

rubraview_page_source_t rubraview_page_source_from_listing(proven_arena_t *arena,
                                                            const rubraview_fs_listing_t *listing,
                                                            u8str_t extension_filter,
                                                            rubraview_sort_mode_t mode,
                                                            bool ascending) {
    rubraview_page_source_t source = { .kind = RUBRAVIEW_PAGE_SOURCE_FOLDER };
    if (!arena || !listing || listing->count == 0) return source;

    rubraview_sibling_index_t index = rubraview_fs_index_siblings(
        arena, listing, (u8str_t){ .ptr = "", .len = 0 }, extension_filter, mode, ascending);
    if (index.count == 0) return source;

    ref_buf_t buf = {0};
    for (size_t i = 0; i < index.count; ++i) {
        rubraview_page_ref_t ref = {
            .name = rubraview_path_basename(index.paths[i]),
            .path = index.paths[i],
            .entry_index = 0,
        };
        if (!ref_buf_push(arena, &buf, ref)) break;
    }

    source.pages = buf.data;
    source.page_count = buf.count;
    return source;
}

/* ---- archives, through FultaArc (owner, 2026-10-08, D-74) ----
   One reader for ZIP, 7z and RAR. FultaArc asks for bytes through a source
   and for a set's other volumes by name; both are the caller's mapping and
   mappings made here. What it does not keep — the password given, the
   cancel request, how far a read has got — is this file's `io`. */

typedef struct ps_volume {
    rubraview_fs_mapping_t map;
    struct rubraview_page_source_io *io;
} ps_volume_t;

struct rubraview_page_source_io {
    char  *name;                 /* the first volume's path, NUL-terminated (volume names come from it) */
    char  *password;             /* UTF-8, NUL-terminated; NULL when none was given */
    rubraview_fs_mapping_t first;/* the set's first volume, when the caller opened a later one */
    bool   first_mapped;
    atomic_bool cancel;
    atomic_uint_fast64_t done, total;
    atomic_size_t volumes;       /* volumes read so far, the first included */
    atomic_uint_fast64_t volume_bytes;
    uint64_t first_bytes;
    size_t last;                 /* the entry decoded last, for what a solid read costs next */
    bool   has_last;
};

static fulta_arc_err_t ps_mem_read(void *ctx, uint64_t offset, void *buf, size_t n, size_t *got) {
    const rubraview_fs_mapping_t *map = (const rubraview_fs_mapping_t*)ctx;
    *got = 0;
    if (offset >= map->size) return FULTA_ARC_OK;
    uint64_t left = map->size - offset;
    if ((uint64_t)n > left) n = (size_t)left;
    memcpy(buf, map->data + offset, n);
    *got = n;
    return FULTA_ARC_OK;
}

static void ps_volume_close(void *ctx) {
    ps_volume_t *v = (ps_volume_t*)ctx;      /* its map is the struct's first member */
    rubraview_pal_fs_unmap(&v->map);
    free(v);
}

/* A set's next volume, by the name its format gives it, mapped like the first. */
static fulta_arc_err_t ps_volume_open(void *ctx, uint32_t index, const char *name, fulta_arc_source_t *out) {
    struct rubraview_page_source_io *io = (struct rubraview_page_source_io*)ctx;
    (void)index;
    if (!name || !name[0]) return FULTA_ARC_ERR_VOLUME_MISSING;
    ps_volume_t *v = (ps_volume_t*)calloc(1, sizeof(*v));
    if (!v) return FULTA_ARC_ERR_NOMEM;
    v->io = io;
    if (!rubraview_pal_fs_map((u8str_t){ .ptr = name, .len = strlen(name) }, &v->map)) { free(v); return FULTA_ARC_ERR_VOLUME_MISSING; }
    if (v->map.size > (uint64_t)SIZE_MAX) { rubraview_pal_fs_unmap(&v->map); free(v); return FULTA_ARC_ERR_VOLUME_MISSING; }
    atomic_fetch_add(&io->volumes, 1);
    atomic_fetch_add(&io->volume_bytes, v->map.size);
    *out = (fulta_arc_source_t){ .ctx = v, .size = v->map.size, .read_at = ps_mem_read, .close = ps_volume_close };
    return FULTA_ARC_OK;
}

/* The password given, once; after that the archive is told there is none,
   so a wrong one is an answer rather than a loop. */
static fulta_arc_err_t ps_password(void *ctx, uint32_t attempt, const char **utf8_out) {
    struct rubraview_page_source_io *io = (struct rubraview_page_source_io*)ctx;
    if (!io->password || attempt > 0) return FULTA_ARC_ERR_PASSWORD_NEEDED;
    *utf8_out = io->password;
    return FULTA_ARC_OK;
}

static int ps_cancelled(void *ctx) {
    return atomic_load(&((struct rubraview_page_source_io*)ctx)->cancel) ? 1 : 0;
}

static char *ps_strdup(u8str_t s) {
    char *out = (char*)malloc(s.len + 1);
    if (!out) return NULL;
    if (s.len) memcpy(out, s.ptr, s.len);
    out[s.len] = '\0';
    return out;
}

static void ps_io_free(struct rubraview_page_source_io *io) {
    if (!io) return;
    if (io->password) {
        volatile char *wipe = (volatile char*)io->password;
        for (size_t k = 0; io->password[k]; ++k) wipe[k] = 0;
        free(io->password);
    }
    if (io->first_mapped) rubraview_pal_fs_unmap(&io->first);
    free(io->name);
    free(io);
}

/* ---- a RAR set's volume names (RARLAB's own naming, from its manual) ----
   `x.part2.rar` is a later volume of `x.part1.rar` (the digits keep their
   width); `x.r00`, `x.r01`, ... `x.s00` follow `x.rar`. */
static bool ext_is(u8str_t name, size_t at, const char *lower3) {
    for (size_t i = 0; i < 3; ++i) {
        char c = name.ptr[at + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != lower3[i]) return false;
    }
    return true;
}

/* Where the digits of `.partN.rar` are, or false. */
static bool rar_part_digits(u8str_t name, size_t *out_at, size_t *out_len) {
    if (name.len < 10 || name.ptr[name.len - 4] != '.' || !ext_is(name, name.len - 3, "rar")) return false;
    size_t end = name.len - 4, at = end;
    while (at > 0 && name.ptr[at - 1] >= '0' && name.ptr[at - 1] <= '9') at--;
    if (at == end || at < 5 || name.ptr[at - 5] != '.') return false;
    static const char part[4] = { 'p', 'a', 'r', 't' };
    for (size_t i = 0; i < 4; ++i) {
        char c = name.ptr[at - 4 + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != part[i]) return false;
    }
    *out_at = at;
    *out_len = end - at;
    return true;
}

/* `.r00` to `.z99` after a `.rar`: the old numbering's later volumes. */
static bool rar_old_later(u8str_t name) {
    if (name.len < 5 || name.ptr[name.len - 4] != '.') return false;
    char c = name.ptr[name.len - 3];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    char d1 = name.ptr[name.len - 2], d2 = name.ptr[name.len - 1];
    return c >= 'r' && c <= 'z' && d1 >= '0' && d1 <= '9' && d2 >= '0' && d2 <= '9';
}

bool rubraview_page_source_is_later_volume(u8str_t name) {
    size_t at = 0, len = 0;
    if (rar_part_digits(name, &at, &len)) {
        bool first = name.ptr[at + len - 1] == '1';
        for (size_t i = 0; i + 1 < len; ++i) first = first && name.ptr[at + i] == '0';
        return !first;
    }
    return rar_old_later(name);
}

/* The first volume's path for a later volume's, in `out` (malloc); NULL when `path` is not one. */
static char *rar_first_volume_path(u8str_t path) {
    size_t at = 0, len = 0;
    if (!rubraview_page_source_is_later_volume(path)) return NULL;
    char *out = ps_strdup(path);
    if (!out) return NULL;
    if (rar_part_digits(path, &at, &len)) {
        for (size_t i = 0; i < len; ++i) out[at + i] = i + 1 == len ? '1' : '0';
    } else {
        memcpy(out + path.len - 3, "rar", 3);
    }
    return out;
}

/* ---- names ----
   FultaArc gives every name as UTF-8. Where an archive does not say its
   names are Unicode and they are not valid UTF-8, FultaArc's own default is
   the format's (CP437); the reader's is the machine's code page (§3.8.3),
   so those names are read again here from their stored bytes. A code page
   the reader chose is FultaArc's to apply (Shift+N, D-67). */
static fulta_arc_codepage_t ps_codepage(rubraview_codepage_t choice) {
    switch (choice) {
        case RUBRAVIEW_CODEPAGE_UTF8:                return FULTA_ARC_CP_UTF8;
        case RUBRAVIEW_CODEPAGE_KOREAN:              return FULTA_ARC_CP_949;
        case RUBRAVIEW_CODEPAGE_JAPANESE:            return FULTA_ARC_CP_932;
        case RUBRAVIEW_CODEPAGE_SIMPLIFIED_CHINESE:  return FULTA_ARC_CP_936;
        case RUBRAVIEW_CODEPAGE_TRADITIONAL_CHINESE: return FULTA_ARC_CP_950;
        case RUBRAVIEW_CODEPAGE_WESTERN:             return FULTA_ARC_CP_1252;
        default:                                     return FULTA_ARC_CP_AUTO;
    }
}

static u8str_t ps_display_name(proven_arena_t *arena, const fulta_arc_entry_t *en, fulta_arc_format_t format,
                               rubraview_codepage_t override_choice) {
    /* Copied into the arena: a page's name outlives the archive it came from
       (FultaArc frees its own when the archive closes). */
    u8str_t name = { .ptr = "", .len = 0 };
    size_t given = en->name ? strlen(en->name) : 0;
    proven_result_mem_mut_t own = proven_arena_alloc(arena, given + 1);
    if (proven_is_ok(own.err)) {
        if (given) memcpy(own.value.ptr, en->name, given);
        ((char*)own.value.ptr)[given] = '\0';
        name = (u8str_t){ .ptr = (const char*)own.value.ptr, .len = given };
    }
    if (override_choice != RUBRAVIEW_CODEPAGE_AUTO || format == FULTA_ARC_FORMAT_7Z || !en->name_raw) return name;
    u8str_t raw = { .ptr = (const char*)en->name_raw, .len = en->name_raw_size };
    if (raw.len == 0 || rubraview_utf8_validate(raw)) return name;
    uint32_t codepage = 0;
    if (!rubraview_archive_filename_plan(raw, false, RUBRAVIEW_CODEPAGE_AUTO, &codepage)) return name;
    u8str_t decoded = rubraview_pal_transcode_codepage(arena, raw, codepage);
    if (decoded.len == 0) return name;                 /* a platform with no tables: FultaArc's reading stands */
    proven_result_mem_mut_t res = proven_arena_alloc(arena, decoded.len + 1);
    if (!proven_is_ok(res.err)) return name;
    char *out = (char*)res.value.ptr;
    for (size_t i = 0; i < decoded.len; ++i) out[i] = decoded.ptr[i] == '\\' ? '/' : decoded.ptr[i];
    out[decoded.len] = '\0';
    return (u8str_t){ .ptr = out, .len = decoded.len };
}

/* ---- reading an entry ---- */

typedef struct ps_sink {
    uint8_t *out;                 /* NULL: counted and dropped */
    size_t   cap, len;
    struct rubraview_page_source_io *io;
} ps_sink_t;

static fulta_arc_err_t ps_sink_write(void *ctx, const void *data, size_t n) {
    ps_sink_t *s = (ps_sink_t*)ctx;
    if (s->out) {
        if (n > s->cap - s->len) return FULTA_ARC_ERR_LIMIT;
        memcpy(s->out + s->len, data, n);
    }
    s->len += n;
    atomic_fetch_add(&s->io->done, n);
    return FULTA_ARC_OK;
}

/* Where a RAR's solid run starts for `index`: RAR marks each file that
   continues the one before it, so the run is exact. (A 7z marks every file
   of a block alike; FultaArc skips inside the block by itself.) */
static size_t ps_rar_chain_start(const rubraview_page_source_t *source, size_t index) {
    size_t at = index;
    while (at > 0) {
        const fulta_arc_entry_t *en = fulta_arc_entry(source->arc, at);
        if (!en || !(en->flags & FULTA_ARC_ENTRY_SOLID)) break;
        at--;
    }
    return at;
}

/* The entries a read of `index` decodes first, [from, index): none when it is compressed on its own. */
static size_t ps_decode_from(const rubraview_page_source_t *source, size_t index) {
    const fulta_arc_entry_t *en = fulta_arc_entry(source->arc, index);
    if (!en || !(en->flags & FULTA_ARC_ENTRY_SOLID)) return index;
    size_t from = index;
    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_RAR) {
        from = ps_rar_chain_start(source, index);
    } else {
        while (from > 0) {
            const fulta_arc_entry_t *before = fulta_arc_entry(source->arc, from - 1);
            if (!before || !(before->flags & FULTA_ARC_ENTRY_SOLID)) break;
            from--;
        }
    }
    const struct rubraview_page_source_io *io = source->io;
    if (io->has_last && io->last >= from && io->last < index) from = io->last + 1;   /* the stream is already there */
    return from;
}

/* Decode entry `entry` into `out` (or nowhere). A RAR's solid run is walked
   here, file by file, so the progress counts all of it. */
static fulta_arc_err_t ps_extract(rubraview_page_source_t *source, size_t entry, uint8_t *out, size_t cap, size_t *out_len) {
    struct rubraview_page_source_io *io = source->io;
    fulta_arc_err_t err = FULTA_ARC_OK;
    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_RAR) {
        for (size_t i = ps_decode_from(source, entry); i < entry && err == FULTA_ARC_OK; ++i) {
            ps_sink_t drop = { .out = NULL, .io = io };
            fulta_arc_sink_t sink = { .ctx = &drop, .write = ps_sink_write };
            err = fulta_arc_extract(source->arc, i, &sink);
            if (err == FULTA_ARC_OK) { io->last = i; io->has_last = true; }
        }
    }
    ps_sink_t take = { .out = out, .cap = cap, .io = io };
    if (err == FULTA_ARC_OK) {
        fulta_arc_sink_t sink = { .ctx = &take, .write = ps_sink_write };
        err = fulta_arc_extract(source->arc, entry, &sink);
    }
    io->has_last = err == FULTA_ARC_OK;
    io->last = entry;
    if (out_len) *out_len = take.len;
    return err;
}

/* The bytes a read of entry `entry` decodes before it reaches it. */
static uint64_t ps_entry_cost(const rubraview_page_source_t *source, size_t entry) {
    uint64_t cost = 0;
    for (size_t i = ps_decode_from(source, entry); i < entry; ++i) {
        const fulta_arc_entry_t *en = fulta_arc_entry(source->arc, i);
        if (en) cost += en->size;
    }
    return cost;
}

static u8str_t ps_read_entry(proven_arena_t *arena, rubraview_page_source_t *source, size_t entry, uint32_t max_entry_bytes,
                             fulta_arc_err_t *out_err) {
    u8str_t none = { .ptr = "", .len = 0 };
    const fulta_arc_entry_t *en = fulta_arc_entry(source->arc, entry);
    *out_err = FULTA_ARC_ERR_INVALID_ARG;
    if (!en || (en->flags & (FULTA_ARC_ENTRY_DIR | FULTA_ARC_ENTRY_UNKNOWN_SIZE))) return none;
    if (en->size > (uint64_t)max_entry_bytes || en->size > SIZE_MAX - 1) { *out_err = FULTA_ARC_ERR_LIMIT; return none; }
    proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)en->size + 1);
    if (!proven_is_ok(res.err)) { *out_err = FULTA_ARC_ERR_NOMEM; return none; }
    uint8_t *out = (uint8_t*)res.value.ptr;
    size_t len = 0;
    /* A RAR's run is decoded through the sinks here and counted whole; a 7z
       skips inside its block out of sight, so only the page itself counts. */
    atomic_store(&source->io->done, 0);
    atomic_store(&source->io->total,
                 (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_RAR ? ps_entry_cost(source, entry) : 0) + en->size);
    *out_err = ps_extract(source, entry, out, (size_t)en->size, &len);
    if (*out_err != FULTA_ARC_OK) return none;
    out[len] = 0;                                            /* §7.2.3: a text parser may take it as it is */
    return (u8str_t){ .ptr = (const char*)out, .len = len };
}

static bool err_is_password(fulta_arc_err_t err) {
    return err == FULTA_ARC_ERR_PASSWORD_NEEDED || err == FULTA_ARC_ERR_PASSWORD_WRONG;
}

rubraview_page_source_t rubraview_page_source_from_archive(proven_arena_t *arena,
                                                            const uint8_t *data, size_t size,
                                                            u8str_t archive_path,
                                                            u8str_t extension_filter,
                                                            rubraview_codepage_t override_choice,
                                                            uint32_t max_entry_bytes,
                                                            uint64_t max_block_bytes) {
    return rubraview_page_source_from_archive_password(arena, data, size, archive_path, extension_filter, override_choice,
                                                       max_entry_bytes, max_block_bytes, (u8str_t){ .ptr = "", .len = 0 });
}

rubraview_page_source_t rubraview_page_source_from_archive_password(proven_arena_t *arena,
                                                                     const uint8_t *data, size_t size,
                                                                     u8str_t archive_path,
                                                                     u8str_t extension_filter,
                                                                     rubraview_codepage_t override_choice,
                                                                     uint32_t max_entry_bytes,
                                                                     uint64_t max_block_bytes,
                                                                     u8str_t password) {
    rubraview_page_source_t source = { .kind = RUBRAVIEW_PAGE_SOURCE_ARCHIVE, .archive_path = archive_path };
    if (!arena || !data || size == 0) return source;
    (void)max_block_bytes;   /* the old 7z reader's block held in memory; FultaArc streams and holds none */

    struct rubraview_page_source_io *io = (struct rubraview_page_source_io*)calloc(1, sizeof(*io));
    if (!io) return source;
    source.io = io;
    io->first_bytes = size;
    if (password.len > 0) io->password = ps_strdup(password);

    /* A set opened by a later volume's name is read from its first (owner, 2026-10-01). */
    rubraview_fs_mapping_t whole = { .data = data, .size = size };
    io->name = rar_first_volume_path(archive_path);
    if (io->name) {
        if (rubraview_pal_fs_map((u8str_t){ .ptr = io->name, .len = strlen(io->name) }, &io->first) &&
            io->first.size <= (uint64_t)SIZE_MAX) {
            io->first_mapped = true;
            whole = io->first;
            io->first_bytes = io->first.size;
        } else {
            if (io->first.os) rubraview_pal_fs_unmap(&io->first);
            free(io->name);
            io->name = NULL;                                  /* its first volume is not there: read alone */
        }
    }
    if (!io->name) io->name = ps_strdup(archive_path);
    atomic_store(&io->volumes, 1);

    /* The mapping lives in `io` so that its address outlives this call. */
    if (!io->first_mapped) io->first = whole;
    fulta_arc_source_t src = { .ctx = &io->first, .size = whole.size, .read_at = ps_mem_read, .close = NULL };
    fulta_arc_options_t opt = {
        .name = archive_path.len > 0 ? io->name : NULL,
        .volume = ps_volume_open, .volume_ctx = io,
        .password = ps_password, .password_ctx = io,
        .codepage = ps_codepage(override_choice),
        .max_dictionary = 0,                                  /* FultaArc's own bound, 1 GiB */
        .cancel = ps_cancelled, .cancel_ctx = io,
    };
    fulta_arc_err_t err = fulta_arc_open(&src, &opt, &source.arc);
    if (err != FULTA_ARC_OK) {
        source.arc = NULL;
        if (err_is_password(err)) {                            /* its headers are locked */
            source.needs_password = true;
            source.password_wrong = password.len > 0;
        }
        return source;
    }
    fulta_arc_format_t format = fulta_arc_format(source.arc);
    source.kind = format == FULTA_ARC_FORMAT_7Z ? RUBRAVIEW_PAGE_SOURCE_ARCHIVE_7Z
                : format == FULTA_ARC_FORMAT_RAR ? RUBRAVIEW_PAGE_SOURCE_ARCHIVE_RAR
                : RUBRAVIEW_PAGE_SOURCE_ARCHIVE;

    size_t count = fulta_arc_count(source.arc);
    proven_result_mem_mut_t items_res = rubraview_arena_alloc_array(arena, count + 1, sizeof(rubraview_sort_item_t));
    if (!proven_is_ok(items_res.err)) return source;
    rubraview_sort_item_t *items = (rubraview_sort_item_t*)(void*)items_res.value.ptr;

    size_t kept = 0, comicinfo = SIZE_MAX, locked = SIZE_MAX;
    for (size_t i = 0; i < count; ++i) {
        const fulta_arc_entry_t *en = fulta_arc_entry(source.arc, i);
        if (!en || (en->flags & (FULTA_ARC_ENTRY_DIR | FULTA_ARC_ENTRY_UNSUPPORTED))) continue;
        u8str_t name = ps_display_name(arena, en, format, override_choice);
        if (name_is_comicinfo(name)) { comicinfo = i; continue; }
        if (!rubraview_glob_match_list(rubraview_path_basename(name), extension_filter)) continue;
        if ((en->flags & FULTA_ARC_ENTRY_ENCRYPTED) && locked == SIZE_MAX) locked = i;
        items[kept++] = (rubraview_sort_item_t){
            .name = name,
            .mtime = 0, .ctime = 0,
            .size_bytes = en->size > UINT32_MAX ? UINT32_MAX : (uint32_t)en->size,
            .tag = (uint64_t)i,
        };
    }

    /* Its pages are locked: the password is tried on one of them now, the
       first in the archive (in a solid one, the cheapest to reach), so a
       book that cannot be read says so instead of opening blank. */
    if (locked != SIZE_MAX) {
        fulta_arc_err_t tried = FULTA_ARC_ERR_PASSWORD_NEEDED;
        if (password.len > 0) {
            atomic_store(&io->done, 0);
            tried = ps_extract(&source, locked, NULL, 0, NULL);
        }
        if (tried != FULTA_ARC_OK) {
            if (err_is_password(tried) || tried == FULTA_ARC_ERR_CHECKSUM || tried == FULTA_ARC_ERR_CORRUPT) {
                source.needs_password = true;
                source.password_wrong = password.len > 0;
            }
            return source;
        }
    }

    /* §3.8.5: the manifest is read now, while the archive is open. */
    if (comicinfo != SIZE_MAX) {
        fulta_arc_err_t xml_err = FULTA_ARC_OK;
        u8str_t xml = ps_read_entry(arena, &source, comicinfo, max_entry_bytes, &xml_err);
        if (xml_err == FULTA_ARC_OK) {
            source.has_comicinfo = true;
            source.comicinfo_xml = xml;
        }
    }

    /* §3.8.1: pages are numbered, so natural order is the right order —
       folder by folder, the way the book's own folders read (owner,
       2026-09-28: comics with complicated paths). */
    rubraview_sort_items(items, kept, RUBRAVIEW_SORT_PATH_NATURAL, true, NULL);

    ref_buf_t buf = {0};
    for (size_t i = 0; i < kept; ++i) {
        rubraview_page_ref_t ref = { .name = items[i].name, .path = { .ptr = "", .len = 0 }, .entry_index = (size_t)items[i].tag };
        if (!ref_buf_push(arena, &buf, ref)) break;
    }
    source.pages = buf.data;
    source.page_count = buf.count;
    return source;
}

void rubraview_page_source_close(rubraview_page_source_t *source) {
    if (!source) return;
    if (source->arc) fulta_arc_close(source->arc);         /* closes the volumes it asked for */
    source->arc = NULL;
    ps_io_free(source->io);
    source->io = NULL;
    source->pages = NULL;
    source->page_count = 0;
}

rubraview_page_bytes_t rubraview_page_source_read(proven_arena_t *arena,
                                                   rubraview_page_source_t *source,
                                                   size_t index,
                                                   uint32_t max_entry_bytes) {
    rubraview_page_bytes_t result = { .data = { .ptr = "", .len = 0 }, .from_disk = false, .ok = false };
    if (!source || index >= source->page_count) return result;

    if (source->kind == RUBRAVIEW_PAGE_SOURCE_FOLDER) {
        /* Nothing to decompress: the caller opens the path itself, which
           lets the image PAL decode straight from the file. */
        result.from_disk = true;
        result.ok = source->pages[index].path.len > 0;
        return result;
    }
    if (!source->arc || !arena) return result;

    fulta_arc_err_t err = FULTA_ARC_OK;
    u8str_t data = ps_read_entry(arena, source, source->pages[index].entry_index, max_entry_bytes, &err);
    if (err != FULTA_ARC_OK) return result;
    result.data = data;
    result.ok = true;
    return result;
}

const fulta_arc_entry_t *rubraview_page_source_entry(const rubraview_page_source_t *source, size_t index) {
    if (!source || !source->arc || index >= source->page_count) return NULL;
    return fulta_arc_entry(source->arc, source->pages[index].entry_index);
}

uint64_t rubraview_page_source_entry_size(const rubraview_page_source_t *source, size_t index) {
    const fulta_arc_entry_t *en = rubraview_page_source_entry(source, index);
    return en ? en->size : 0;
}

u8str_t rubraview_page_source_format_name(const rubraview_page_source_t *source) {
    if (!source || !source->arc) return (u8str_t){ .ptr = "", .len = 0 };
    const char *name = fulta_arc_format_name(fulta_arc_format(source->arc));
    return (u8str_t){ .ptr = name ? name : "", .len = name ? strlen(name) : 0 };
}

static bool ps_any_flag(const rubraview_page_source_t *source, uint32_t flag) {
    if (!source || !source->arc) return false;
    for (size_t i = 0, n = fulta_arc_count(source->arc); i < n; ++i) {
        const fulta_arc_entry_t *en = fulta_arc_entry(source->arc, i);
        if (en && (en->flags & flag)) return true;
    }
    return false;
}

bool rubraview_page_source_is_solid(const rubraview_page_source_t *source) {
    return ps_any_flag(source, FULTA_ARC_ENTRY_SOLID);
}

bool rubraview_page_source_is_encrypted(const rubraview_page_source_t *source) {
    return ps_any_flag(source, FULTA_ARC_ENTRY_ENCRYPTED);
}

size_t rubraview_page_source_volumes(const rubraview_page_source_t *source, uint64_t *out_bytes) {
    if (out_bytes) *out_bytes = 0;
    if (!source || !source->io) return 0;
    size_t volumes = atomic_load(&source->io->volumes);
    if (volumes < 2) return 0;
    if (out_bytes) *out_bytes = source->io->first_bytes + atomic_load(&source->io->volume_bytes);
    return volumes;
}

size_t rubraview_page_source_read_budget(const rubraview_page_source_t *source, size_t index) {
    uint64_t size = rubraview_page_source_entry_size(source, index);
    if (size == 0 || size > SIZE_MAX - 256) return 0;
    /* One allocation of size + 1; the rest is alignment. */
    return (size_t)size + 256;
}

uint64_t rubraview_page_source_read_cost(const rubraview_page_source_t *source, size_t index) {
    if (!source || !source->arc || index >= source->page_count) return 0;
    return ps_entry_cost(source, source->pages[index].entry_index);
}

void rubraview_page_source_cancel(rubraview_page_source_t *source, bool cancel) {
    if (source && source->io) atomic_store(&source->io->cancel, cancel);
}

void rubraview_page_source_progress(const rubraview_page_source_t *source, uint64_t *out_done, uint64_t *out_total) {
    if (out_done) *out_done = 0;
    if (out_total) *out_total = 0;
    if (!source || !source->io) return;
    if (out_done) *out_done = atomic_load(&source->io->done);
    if (out_total) *out_total = atomic_load(&source->io->total);
}

int32_t rubraview_page_source_find(const rubraview_page_source_t *source, u8str_t path) {
    if (!source || path.len == 0) return -1;

    u8str_t wanted = rubraview_path_basename(path);
    if (wanted.len == 0) return -1;

    /* The name alone is what is compared. A full path, a relative path
       and a bare filename all name the same picture once the folder is
       open, and only the name survives all three. */
    for (size_t i = 0; i < source->page_count; ++i) {
        u8str_t candidate = source->pages[i].path.len > 0
                              ? rubraview_path_basename(source->pages[i].path)
                              : source->pages[i].name;
        if (rubraview_path_same(candidate, wanted)) return (int32_t)i;
    }
    return -1;
}

u8str_t rubraview_page_source_sibling_archive(proven_arena_t *arena,
                                              const rubraview_fs_listing_t *listing,
                                              u8str_t current_archive_path,
                                              bool forward) {
    u8str_t none = { .ptr = "", .len = 0 };
    if (!arena || !listing || listing->count == 0) return none;

    /* Order the directory's archives the way the reader sees them, then
       step one place — that is what makes Vol 01 run into Vol 02. */
    rubraview_sibling_index_t archives = rubraview_fs_index_siblings(
        arena, listing, current_archive_path, U8(ARCHIVE_FILTER), RUBRAVIEW_SORT_NAME_NATURAL, true);
    if (archives.count == 0 || !archives.found) return none;

    /* A RAR set is one book (owner, 2026-10-01): its later volumes are stepped over. */
    size_t at = archives.current;
    for (;;) {
        if (forward ? at + 1 >= archives.count : at == 0) return none;
        at = forward ? at + 1 : at - 1;
        if (!rubraview_page_source_is_later_volume(archives.paths[at])) return archives.paths[at];
    }
}
