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
size_t rubraview_arcedit_converted_name(u8str_t archive_name, rubraview_arcedit_target_t target, char *dst, size_t cap);

/**
 * An entry's name with its last part replaced: `ch1/003.jpg` and
 * `cover.jpg` give `ch1/cover.jpg`. Same return as above.
 */
size_t rubraview_arcedit_sibling_name(u8str_t entry_name, u8str_t new_base, char *dst, size_t cap);

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
    const char *name;                  /* a name without folders */
    const uint8_t *data;
    size_t size;
    rubraview_arcedit_target_t target;
    const char *out_path;              /* where the new archive is written; created or replaced */

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

/** Give back what a run allocated (`volumes`). */
void rubraview_arcedit_job_free(rubraview_arcedit_job_t *job);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_ARCEDIT_H */
