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

typedef enum rubraview_pack_format { RUBRAVIEW_PACK_ZIP = 0, RUBRAVIEW_PACK_7Z } rubraview_pack_format_t;

typedef struct rubraview_compress_job {
    /* given */
    const char *const *sources;        /* files and folders, UTF-8 full paths; each goes in by its own name */
    size_t source_count;
    const char *output;                /* the archive wanted; a free name next to it when one is there */
    rubraview_pack_format_t format;
    int level;                         /* 0 store ... 9 smallest */
    const char *password;              /* NULL or "": none. ZIP: WinZip AES-256; 7z: AES-256 (its names stay readable) */
    /* while it runs */
    _Atomic bool cancel;
    _Atomic uint64_t done, total;      /* bytes packed, then bytes checked */
    _Atomic int phase;                 /* 0 listing, 1 packing, 2 checking */
    /* when it is done */
    rubraview_archive_result_t result;
    uint32_t files, folders;
    char written[RUBRAVIEW_ARCHIVE_PATH];   /* the archive made */
    char why[256];
} rubraview_compress_job_t;

void rubraview_compress_run(rubraview_compress_job_t *job);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_ARCHIVE_JOB_H */
