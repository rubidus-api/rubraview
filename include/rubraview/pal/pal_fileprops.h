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

/* ---- RV-281 phase 2 (pal_security_win32.c) ---- */

/** The file's own signatures (the primary, then the nested ones) into `out`, each checked offline. Returns how many. */
size_t rubraview_pal_fileprops_signatures(const char *path, rubraview_props_sig_t *out, size_t cap);

typedef struct rubraview_props_security {
    char     owner_sid[192], owner[256];
    bool     read_ok;              /* false: `why` says what Windows said (Access is denied.) */
    bool     protect;              /* inheritance off */
    bool     no_dacl;              /* no list at all: everyone may do everything */
    uint32_t effective;            /* what the user running Rubraview may do (AuthzAccessCheck) */
    char     why[256];
} rubraview_props_security_t;

/** The owner, the inheritance and the permissions' ACEs (in the order Windows keeps them). Returns how many ACEs. */
size_t rubraview_pal_fileprops_security_read(const char *path, rubraview_props_security_t *sec, rubraview_props_ace_t *aces, size_t cap);

/**
 * A new owner (`change->owner_sid`, when set) and, with `write_dacl`, the explicit ACEs given (inherited ones are
 * Windows', added by it), inheritance off when `change->protect` is 1. False, with Windows' words in `why`.
 */
bool rubraview_pal_fileprops_security_write(const char *path, const rubraview_props_sec_t *change, const rubraview_props_ace_t *aces,
                                            size_t count, bool write_dacl, char *why, size_t cap);

/** The audit entries (SACL): only with administrator rights (the elevated child). */
size_t rubraview_pal_fileprops_audit_read(const char *path, rubraview_props_ace_t *aces, size_t cap, bool *protect, char *why, size_t why_cap);
bool rubraview_pal_fileprops_audit_write(const char *path, const rubraview_props_ace_t *aces, size_t count, bool protect, char *why, size_t cap);

/** A user's or a group's name as typed ("Guests", "PC\\me") to its SID and its full name; false when Windows knows none. */
bool rubraview_pal_fileprops_account(const char *typed, char *sid, size_t sid_cap, char *name, size_t name_cap);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_FILEPROPS_H */
