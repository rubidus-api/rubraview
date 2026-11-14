#ifndef RUBRAVIEW_PAL_FILEPROPS_H
#define RUBRAVIEW_PAL_FILEPROPS_H

#include "rubraview/core.h"
#include "rubraview/fileprops.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-281: what the properties window reads and writes through Windows' own system calls in the viewer (no other
 * program's code: the Details tab's property handlers and Windows' own dialogs are the child's, pal_shellverbs.h).
 */

/** A file or folder (UTF-8 path) into `out`; a folder's contents counted for `budget_ms` at most. False when it is not there. */
bool rubraview_pal_fileprops_read(const char *path, rubraview_fileprops_file_t *out, uint32_t budget_ms);

/**
 * Its attributes from `have` to `want` (RUBRAVIEW_FATTR_*): read-only, hidden, archive and indexed by its attribute
 * bits, compression by FSCTL_SET_COMPRESSION, encryption by EncryptFile / DecryptFile. False, with Windows' words in
 * `why`, at the first that fails.
 */
bool rubraview_pal_fileprops_set_attributes(const char *path, uint32_t have, uint32_t want, char *why, size_t cap);

/** "Unblock": the Zone.Identifier stream that says it came from another computer, removed. */
bool rubraview_pal_fileprops_unblock(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_FILEPROPS_H */
