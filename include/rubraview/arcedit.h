#ifndef RUBRAVIEW_ARCEDIT_H
#define RUBRAVIEW_ARCEDIT_H

#include "rubraview/core.h"
#include "rubraview/pagesource.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * D-88 (R149): a page inside an archive deleted, renamed, written over
 * or joined by a copy. The archive is never changed where it lies: the
 * edited book is written to a file beside it by FultaArc, opened again
 * and compared with what it should hold, and only then does the caller
 * put it in the old one's place.
 *
 * A ZIP, or a 7z whose files are each packed on their own, keeps its
 * format and its untouched entries byte for byte. Anything else — a solid
 * 7z, a RAR, an ALZ, an EGG, a split set — is written again as a ZIP or a
 * 7z. An encrypted archive is refused (owner, 2026-10-10).
 */

typedef enum rubraview_arcedit_route {
    RUBRAVIEW_ARCEDIT_IN_PLACE = 0,   /* the same format, the same name */
    RUBRAVIEW_ARCEDIT_CONVERT,        /* a new ZIP or 7z; the reader is asked which */
    RUBRAVIEW_ARCEDIT_ENCRYPTED,      /* refused */
    RUBRAVIEW_ARCEDIT_NOT_AN_ARCHIVE, /* a folder, or nothing open */
} rubraview_arcedit_route_t;

/** What changing a page of this open archive takes. */
rubraview_arcedit_route_t rubraview_arcedit_route(const rubraview_page_source_t *source);

typedef enum rubraview_arcedit_target {
    RUBRAVIEW_ARCEDIT_SAME = 0,       /* the archive's own format (the in-place route) */
    RUBRAVIEW_ARCEDIT_ZIP,
    RUBRAVIEW_ARCEDIT_7Z,
} rubraview_arcedit_target_t;

/**
 * The file name a converted book takes: the old name with the new
 * format's extension — `.cbz` / `.cb7` for a comic (`.cbr`, `.cbz`,
 * `.cb7`), `.zip` / `.7z` otherwise. A split set's volume mark goes
 * (`x.part1.rar`, `x.7z.001`, `x.vol1.egg` give `x.zip`). Written with
 * its NUL when `cap` holds it; returns the length, 0 when it does not fit.
 */
size_t rubraview_arcedit_converted_name(proven_u8str_view_t archive_name, rubraview_arcedit_target_t target, char *dst, size_t cap);

/**
 * An entry's name with its last part replaced: `ch1/003.jpg` and
 * `cover.jpg` give `ch1/cover.jpg`. Same return as above.
 */
size_t rubraview_arcedit_sibling_name(proven_u8str_view_t entry_name, proven_u8str_view_t new_base, char *dst, size_t cap);

typedef enum rubraview_arcedit_op {
    RUBRAVIEW_ARCEDIT_DELETE = 0,
    RUBRAVIEW_ARCEDIT_RENAME,     /* `name` is the entry's new name, in the folder it is in */
    RUBRAVIEW_ARCEDIT_REPLACE,    /* `data` are its new bytes; `name` its new name, or NULL to keep it */
    RUBRAVIEW_ARCEDIT_ADD,        /* a new entry `name` holding `data`, in the entry's folder */
} rubraview_arcedit_op_t;

typedef enum rubraview_arcedit_result {
    RUBRAVIEW_ARCEDIT_OK = 0,
    RUBRAVIEW_ARCEDIT_CANCELLED,
    RUBRAVIEW_ARCEDIT_REFUSED_ENCRYPTED,
    RUBRAVIEW_ARCEDIT_UNREADABLE,     /* the archive could not be opened or read */
    RUBRAVIEW_ARCEDIT_UNWRITABLE,     /* the new file could not be written */
    RUBRAVIEW_ARCEDIT_NAME_TAKEN,     /* an entry already has that name */
    RUBRAVIEW_ARCEDIT_BAD_NAME,
    RUBRAVIEW_ARCEDIT_NOT_VERIFIED,   /* written, but it does not hold what it should */
    RUBRAVIEW_ARCEDIT_NO_MEMORY,
    RUBRAVIEW_ARCEDIT_RESULT_COUNT
} rubraview_arcedit_result_t;

/** One line for the reader. */
const char *rubraview_arcedit_result_text(rubraview_arcedit_result_t result);

/* One edit. The caller fills the first part and owns every pointer in
   it; rubraview_arcedit_run may be called on a thread of its own, and
   `cancel`, `done` and `total` may then be touched from another. */
typedef struct rubraview_arcedit_job {
    const char *archive_path;          /* the archive, or the first volume of its set */
    rubraview_codepage_t codepage;     /* how its names are read, as the viewer reads them */
    rubraview_arcedit_op_t op;
    size_t entry_index;                /* the entry (FultaArc's index) the edit is about */
    const size_t *entries;             /* RV-223: DELETE of these entries (folders' too) in one writing, `entry_index`
                                          not used; NULL: the one entry */
    size_t entry_count;
    const char *name;                  /* a name without folders */
    const uint8_t *data;
    size_t size;
    rubraview_arcedit_target_t target;
    const char *out_path;              /* where the new archive is written; created or replaced */
    int level;                         /* what is packed again: 0 stored as it is (ZIP Store, 7z Copy),
                                          1..9 packed (ZIP Deflate, 7z LZMA), 9 the smallest and slowest */

    atomic_bool cancel;
    atomic_uint_fast64_t done, total;

    /* Out. Every file of the original set that was read, the first
       included: what the caller removes after a conversion. */
    char **volumes;
    size_t volume_count;
    int32_t error;                     /* FultaArc's code, for the log */
} rubraview_arcedit_job_t;

/**
 * Write the edited archive to `out_path` and check it: every entry that
 * should be there is, with its size and (where both archives give one)
 * its checksum, and nothing else; the bytes of a replaced or added entry
 * read back as given. On anything but OK `out_path` is removed. The
 * original is not touched.
 */
rubraview_arcedit_result_t rubraview_arcedit_run(rubraview_arcedit_job_t *job);

/* RV-260 (owner, 2026-11-08: "zip나 7z 같은 압축이 우리가 가능한 압축파일을 루브라뷰 탐색기에서 열고 그 안의 폴더를 보여주고 있을 때, 클립보드 붙이기나 드래그 앤 드롭으로 외부 파일이 들어가면 파일 추가 가능하게 해 주세요."): files from the disk into the archive, each under its full name inside it
   ('/'-separated: a folder's files keep their places). */
typedef struct rubraview_arcedit_add {
    const char *path;   /* the file on the disk */
    const char *name;   /* its name in the archive */
} rubraview_arcedit_add_t;

/**
 * RV-260: the archive written again with `adds` in it, read from the disk as it is written (a large file is never
 * held whole), and the entries `job->entries` (`entry_count`; NULL for none) taken out first — the ones an add
 * replaces. A name another entry keeps, or two adds share, is RUBRAVIEW_ARCEDIT_NAME_TAKEN. Converted (`target`),
 * written and checked as rubraview_arcedit_run is: every entry kept, every add with its size, nothing else.
 * `job->op`, `name`, `data`, `size`, `entry_index` are not used.
 */
rubraview_arcedit_result_t rubraview_arcedit_add_files(rubraview_arcedit_job_t *job, const rubraview_arcedit_add_t *adds, size_t count);

/** RV-260: whether entry `index` holds the same bytes as the file at `path` (its size first, then every byte as it
    is unpacked): 1 yes, 0 no, -1 it could not be told. */
int rubraview_arcedit_entry_same(const char *archive_path, rubraview_codepage_t codepage, size_t index, const char *path);

/** Give back what a run allocated (`volumes`). */
void rubraview_arcedit_job_free(rubraview_arcedit_job_t *job);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_ARCEDIT_H */
