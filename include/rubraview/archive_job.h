#ifndef RUBRAVIEW_ARCHIVE_JOB_H
#define RUBRAVIEW_ARCHIVE_JOB_H

#include "rubraview/core.h"
#include "rubraview/pagesource.h"

#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-207 (owner, 2026-10-31: "알집 반디집 굳이 깔지 않아도 대부분의 압축파일 해제하고 압축할 수 있을 정도로"): an
 * archive's files written out to a folder, and files and folders packed into a new ZIP or 7z. Each runs on the
 * caller's thread (a worker's): the UI reads the counters and may raise `cancel` meanwhile. Nothing already there is
 * written over unless the rule says replace; a file being written is a `.rvpart` beside its place until it is whole;
 * a new archive is read back and compared with its sources before it is kept.
 */

typedef enum rubraview_exists_rule {
    RUBRAVIEW_EXISTS_NUMBER = 0,   /* a new name: photo-1.jpg, photo-2.jpg ... */
    RUBRAVIEW_EXISTS_SKIP,         /* leave the one there */
    RUBRAVIEW_EXISTS_REPLACE,      /* write over it (to the Recycle Bin never: the new one is whole first) */
} rubraview_exists_rule_t;

typedef enum rubraview_extract_mode {
    RUBRAVIEW_EXTRACT_FOLDER = 0,  /* into a folder named after the archive */
    RUBRAVIEW_EXTRACT_SMART,       /* as it is when everything is in one top folder or it is one file, else as FOLDER */
    RUBRAVIEW_EXTRACT_AS_IS,       /* straight into the folder given */
} rubraview_extract_mode_t;

typedef enum rubraview_archive_result {
    RUBRAVIEW_ARCHIVE_DONE = 0,
    RUBRAVIEW_ARCHIVE_PARTLY,          /* some files could not be written or read (`failed`) */
    RUBRAVIEW_ARCHIVE_CANCELLED,
    RUBRAVIEW_ARCHIVE_PASSWORD_NEEDED,
    RUBRAVIEW_ARCHIVE_PASSWORD_WRONG,
    RUBRAVIEW_ARCHIVE_CANNOT_READ,     /* the archive, or a file to pack */
    RUBRAVIEW_ARCHIVE_CANNOT_WRITE,
    RUBRAVIEW_ARCHIVE_CHECK_FAILED,    /* the new archive did not read back as its sources: not kept */
    RUBRAVIEW_ARCHIVE_NOTHING,         /* nothing to do */
    RUBRAVIEW_ARCHIVE_RESULT_COUNT
} rubraview_archive_result_t;

const char *rubraview_archive_result_text(rubraview_archive_result_t result);   /* translated */

/* A name as one folder or file of a Windows path: characters Windows refuses made '_', ending dots and blanks
   dropped, a device name (CON, NUL, COM1 ...) given a leading '_'. Into `out` (`cap` > 0); its length. */
size_t rubraview_archive_safe_part(const char *part, size_t len, char *out, size_t cap);

/* The archive's name without its extension(s), for the folder it is written out to: "book.zip" "book",
   "book.part1.rar" "book", "book.7z.001" "book". */
size_t rubraview_archive_stem(proven_u8str_view_t archive_path, char *out, size_t cap);

/* `path`, or the first of path-1.ext, path-2.ext ... that is not there (`exists` tells), into `out`. */
bool rubraview_archive_free_name(proven_u8str_view_t path, bool (*exists)(proven_u8str_view_t path),
                                 char *out, size_t cap);
/* The same for a folder: the number goes at the end, a dot in its name being no extension ("2024.10.31-1"). */
bool rubraview_archive_free_folder_name(proven_u8str_view_t path, bool (*exists)(proven_u8str_view_t path),
                                        char *out, size_t cap);

#define RUBRAVIEW_ARCHIVE_PATH 4096

typedef struct rubraview_extract_job {
    /* given */
    const char *archive;               /* UTF-8 */
    const char *dest;                  /* the folder it goes into */
    rubraview_extract_mode_t mode;
    rubraview_exists_rule_t exists;
    const size_t *only;                /* the entries picked; NULL: all of them */
    size_t only_count;
    const char *inner;                 /* with `only`: the archive's folder they were picked in ("a/b/"), left off their names */
    const char *password;              /* UTF-8, or NULL */
    rubraview_codepage_t codepage;
    /* while it runs */
    _Atomic bool cancel;
    _Atomic uint64_t done, total;      /* bytes */
    _Atomic uint32_t files;            /* files finished */
    /* when it is done */
    rubraview_archive_result_t result;
    uint32_t written, skipped, failed;
    char root[RUBRAVIEW_ARCHIVE_PATH]; /* the folder the files went into */
    char why[256];                     /* the first thing that went wrong, in words */
} rubraview_extract_job_t;

void rubraview_extract_run(rubraview_extract_job_t *job);

/* Whether the archive asks a password before its files can be read: 1 yes, 0 no, -1 it cannot be read. */
int rubraview_archive_wants_password(const char *archive, rubraview_codepage_t codepage);

/* ---- an archive's tree, for the open-file screen (RV-207) ---- */

typedef struct rubraview_archive_item {
    char *name;                        /* '/' between folders, none at either end */
    uint64_t size;
    int64_t mtime;                     /* seconds since 1970 (as the archive keeps it) */
    bool is_dir, encrypted;
} rubraview_archive_item_t;

typedef struct rubraview_archive_index {
    rubraview_archive_item_t *items;   /* one per entry, in the archive's order: an item's place is its entry's index */
    size_t count;
} rubraview_archive_index_t;

/* Its entries listed (`password` only for an archive whose names are locked). DONE, or why not. */
rubraview_archive_result_t rubraview_archive_index_read(const char *archive, rubraview_codepage_t codepage,
                                                        const char *password, rubraview_archive_index_t *out);
void rubraview_archive_index_free(rubraview_archive_index_t *index);

#define RUBRAVIEW_ARCHIVE_NO_ITEM ((size_t)-1)

typedef struct rubraview_archive_child {
    proven_u8str_view_t name;          /* its own name, a view into an item's */
    bool is_dir;
    size_t item;                       /* its entry; NO_ITEM for a folder only its files name */
    uint64_t size;                     /* a folder: what is in it, all the way down */
    int64_t mtime;
} rubraview_archive_child_t;

/* What is directly in the folder `inner` ("" the top; "a/b"): its folders first, each once, then its files, each in
   the archive's order. Returns how many were stored; a `cap` of the index's count always holds them all. */
size_t rubraview_archive_index_level(const rubraview_archive_index_t *index, proven_u8str_view_t inner,
                                     rubraview_archive_child_t *out, size_t cap);

/* The entries at `path` and under it ("a/b": a/b itself and a/b/...). Returns how many; stored up to `cap`. */
size_t rubraview_archive_index_under(const rubraview_archive_index_t *index, proven_u8str_view_t path, size_t *out, size_t cap);

typedef enum rubraview_pack_format { RUBRAVIEW_PACK_ZIP = 0, RUBRAVIEW_PACK_7Z } rubraview_pack_format_t;

/* D-260: the Compress window's codec. DEFAULT is FultaArc's (ZIP Deflate, 7z LZMA); a level of 0 stores. */
typedef enum rubraview_pack_method {
    RUBRAVIEW_PACK_METHOD_DEFAULT = 0,
    RUBRAVIEW_PACK_METHOD_DEFLATE,
    RUBRAVIEW_PACK_METHOD_BZIP2,
    RUBRAVIEW_PACK_METHOD_LZMA,
    RUBRAVIEW_PACK_METHOD_LZMA2,      /* 7z only */
    RUBRAVIEW_PACK_METHOD_PPMD,
    RUBRAVIEW_PACK_METHOD_ZSTD,       /* ZIP only (stock 7-Zip has no zstd in 7z) */
} rubraview_pack_method_t;

/* The codecs a format takes, its default first, as the window lists them. Returns how many (at most 6). */
size_t rubraview_pack_methods(rubraview_pack_format_t format, rubraview_pack_method_t *out, size_t cap);
bool rubraview_pack_method_fits(rubraview_pack_format_t format, rubraview_pack_method_t method);
/* Whether FultaArc writes it yet: in ZIP only Deflate (its ZIP writer takes no other codec yet, asked for, D-260). */
bool rubraview_pack_method_ready(rubraview_pack_format_t format, rubraview_pack_method_t method);
/* Whether Windows' own ZIP folders read it too (Deflate, or stored); the others need 7-Zip or Bandizip. */
bool rubraview_pack_method_plain(rubraview_pack_format_t format, rubraview_pack_method_t method);
/* The window's level steps (store, fastest, fast, normal, maximum, ultra) as FultaArc's 0..9. */
enum { RUBRAVIEW_PACK_LEVEL_STEPS = 6, RUBRAVIEW_PACK_LEVEL_NORMAL = 3 };
int rubraview_pack_level_of(int step);
/* Split volumes: FultaArc writes none yet (asked for, D-260), so no format splits; a job asking is refused. */
bool rubraview_pack_split_supported(rubraview_pack_format_t format);
/* D-261: a 7z's names hidden behind its password (7-Zip's -mhe): FultaArc writes no encrypted header yet (asked for);
   ZIP has no standard way. A job asking is refused. */
bool rubraview_pack_names_hidden_supported(rubraview_pack_format_t format);
/* D-267: the files there that are volumes of the split set `set` ("x.7z": x.7z.001 ...; "x.zip": x.z01 ... and
   x.zip itself), up to `cap`, each in `out[i]`. Returns how many. */
#define RUBRAVIEW_PACK_PART_PATH (RUBRAVIEW_ARCHIVE_PATH + 16)
size_t rubraview_pack_set_parts(rubraview_pack_format_t format, const char *set, char (*out)[RUBRAVIEW_PACK_PART_PATH], size_t cap);
/* D-262 (owner, 2026-11-02: "압축할 때는 영문 숫자 기호만"): a new archive's password is English letters, digits and
   symbols, `!` to `~`: no space, no Hangul, nothing other programs might encode another way. */
bool rubraview_pack_password_plain(proven_u8str_view_t password);
/* The characters of `in` that are, copied to `out` (cap bytes, NUL-ended); *dropped when any was not. Returns the
   length kept. */
size_t rubraview_password_keep_plain(proven_u8str_view_t in, char *out, size_t cap, bool *dropped);

typedef struct rubraview_compress_job {
    /* given */
    const char *const *sources;        /* files and folders, UTF-8 full paths; each goes in by its own name */
    size_t source_count;
    const char *output;                /* the archive wanted; a free name next to it when one is there */
    rubraview_pack_format_t format;
    int level;                         /* 0 store ... 9 smallest */
    rubraview_pack_method_t method;    /* D-260: DEFAULT, or one rubraview_pack_method_fits and _ready */
    bool solid_off;                    /* D-260, 7z: one block a file (the default packs them together) */
    uint64_t volume_bytes;             /* D-260: 0 one file; otherwise volumes of this size (not written yet) */
    bool names_hidden;                 /* D-261: with a password, the 7z's names encrypted (D-263) */
    const char *password;              /* NULL or "": none. ZIP: WinZip AES-256; 7z: AES-256 (its names stay readable) */
    /* while it runs */
    _Atomic bool cancel;
    _Atomic uint64_t done, total;      /* bytes packed, then bytes checked */
    _Atomic int phase;                 /* 0 listing, 1 packing, 2 checking */
    /* when it is done */
    rubraview_archive_result_t result;
    uint32_t files, folders;
    char written[RUBRAVIEW_ARCHIVE_PATH];   /* the archive made */
    char set_name[RUBRAVIEW_ARCHIVE_PATH];  /* D-267: a split set's name ("x.7z"), kept when it failed too */
    bool older_parts;                       /* D-267: refused for an older set's later volume where this one ends */
    char why[256];
} rubraview_compress_job_t;

void rubraview_compress_run(rubraview_compress_job_t *job);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_ARCHIVE_JOB_H */
