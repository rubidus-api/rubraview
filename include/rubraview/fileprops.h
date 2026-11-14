#ifndef RUBRAVIEW_FILEPROPS_H
#define RUBRAVIEW_FILEPROPS_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-281 (owner, 2026-11-14: "Alt+Enter가 파일의 속성 보기 ... Rubraview 자체 구현 속성창(일반, 디지털서명, 보안, 자세히,
 * 이전버젼 ...)이 뜨고, 그 안에 윈도 기본 속성창 보기 버튼"): the model of Rubraview's own properties window, with no
 * Windows type in it. The PAL reads a file into `rubraview_fileprops_file_t`; the Details tab's rows come from the
 * child process (`--shell-menu-host props-read`) as lines this module reads and writes.
 */

typedef enum rubraview_props_tab {
    RUBRAVIEW_PROPS_GENERAL = 0,
    RUBRAVIEW_PROPS_SIGNATURES,
    RUBRAVIEW_PROPS_SECURITY,
    RUBRAVIEW_PROPS_DETAILS,
    RUBRAVIEW_PROPS_VERSIONS,
    RUBRAVIEW_PROPS_TAB_COUNT
} rubraview_props_tab_t;

/* The attributes the General tab and its Advanced box show (Windows' FILE_ATTRIBUTE_* as the PAL maps them). */
enum {
    RUBRAVIEW_FATTR_READONLY   = 1u << 0,
    RUBRAVIEW_FATTR_HIDDEN     = 1u << 1,
    RUBRAVIEW_FATTR_ARCHIVE    = 1u << 2,
    RUBRAVIEW_FATTR_INDEXED    = 1u << 3,   /* "allow the contents to be indexed": Windows' NOT_CONTENT_INDEXED off */
    RUBRAVIEW_FATTR_COMPRESSED = 1u << 4,
    RUBRAVIEW_FATTR_ENCRYPTED  = 1u << 5,
    RUBRAVIEW_FATTR_ALL        = (1u << 6) - 1u
};

typedef struct rubraview_fileprops_file {
    char     path[1024];       /* UTF-8, whole */
    char     type_name[160];   /* "PNG 파일" as Windows names the type */
    char     opens_with[260];  /* the program a double click starts ("" when none) */
    bool     is_folder;
    bool     blocked;          /* it came from another computer (a Zone.Identifier stream) */
    bool     can_compress;     /* the drive compresses (NTFS) */
    bool     can_encrypt;      /* the system encrypts (EFS: not on Home) */
    uint32_t attributes;       /* RUBRAVIEW_FATTR_* */
    uint64_t size, on_disk;    /* a folder: what is inside it, counted */
    uint32_t files, folders;   /* a folder: what is inside it */
    bool     counted_all;      /* false when counting a folder stopped short */
    int64_t  created, modified, accessed;   /* Unix seconds, 0 for none */
} rubraview_fileprops_file_t;

typedef enum rubraview_tristate { RUBRAVIEW_TRI_OFF = 0, RUBRAVIEW_TRI_ON, RUBRAVIEW_TRI_MIXED } rubraview_tristate_t;

/** One attribute over several files: on for all, off for all, or mixed. */
rubraview_tristate_t rubraview_fileprops_attr_state(const rubraview_fileprops_file_t *files, size_t count, uint32_t attr);

/** What a click on a box makes it: mixed and off become on, on becomes off. */
rubraview_tristate_t rubraview_fileprops_attr_click(rubraview_tristate_t now);

/**
 * The attributes a file gets, from what it has and the boxes as the user left them (`wanted[i]` for attribute bit i,
 * a mixed box leaving each file's own). Compressed and encrypted exclude each other, as in Windows: when both are
 * asked, the one the user turned on last (`last_on`) wins.
 */
uint32_t rubraview_fileprops_attr_apply(uint32_t have, const rubraview_tristate_t wanted[6], uint32_t last_on);

/** The totals of several files (sizes summed, files and folders counted) into `out`; its path is the first one's. */
void rubraview_fileprops_sum(const rubraview_fileprops_file_t *files, size_t count, rubraview_fileprops_file_t *out);

/* ---- the Details tab's rows, as the child sends them ---- */

#define RUBRAVIEW_PROPS_ROW_HEADING  1u   /* a group's heading, no value */
#define RUBRAVIEW_PROPS_ROW_EDITABLE 2u
#define RUBRAVIEW_PROPS_ROW_MULTI    4u   /* several values, "a; b" */
#define RUBRAVIEW_PROPS_ROW_RATING   8u   /* System.Rating: 0..99, shown as stars */

typedef struct rubraview_props_row {
    char     name[96];     /* the canonical name ("System.Title"); "" for a heading */
    char     label[96];    /* the words Windows shows for it */
    char     value[512];   /* as Windows formats it for display */
    uint32_t flags;
} rubraview_props_row_t;

/**
 * Read the child's answer: one row a line, fields split by tabs: "H<TAB>label" for a heading,
 * "P<TAB>name<TAB>label<TAB>flags<TAB>value" for a property (flags: letters e, m, r). Tab, newline and backslash in
 * a field are written \t \n \\. Rows past `cap` are left out. Returns how many were read.
 */
size_t rubraview_props_rows_read(proven_u8str_view_t text, rubraview_props_row_t *rows, size_t cap);

/** One field escaped as the lines carry it, into `out`; false when it does not fit. */
bool rubraview_props_escape(proven_u8str_view_t text, char *out, size_t cap);

/** A change for the child to write: "name<TAB>value" escaped, with a newline; false when it does not fit. */
bool rubraview_props_change_line(const char *name, const char *value, char *out, size_t cap);

/** Change lines read back (the child's side): each into a row's `name` and `value`. Returns how many. */
size_t rubraview_props_changes_read(proven_u8str_view_t text, rubraview_props_row_t *rows, size_t cap);

/** A rating's stars (0..5) from System.Rating's 0..99 as Explorer draws them, and back (1, 25, 50, 75, 99). */
int32_t rubraview_props_rating_stars(int32_t rating);
int32_t rubraview_props_rating_value(int32_t stars);

/* ---- RV-281 phase 2: Digital Signatures ---- */

typedef enum rubraview_sig_status {
    RUBRAVIEW_SIG_UNCHECKED = 0,   /* not verified (a nested signature on a system without per-signature checks) */
    RUBRAVIEW_SIG_OK,
    RUBRAVIEW_SIG_BAD,             /* the file changed since it was signed, or the signature is broken */
    RUBRAVIEW_SIG_UNTRUSTED,       /* well formed, but its chain ends at a root this computer does not trust */
} rubraview_sig_status_t;

/** One of a file's signatures, as the Digital Signatures tab and its details show it (Windows' fields). */
typedef struct rubraview_props_sig {
    char     signer[256];          /* the signer's name */
    char     email[128];           /* "" when the certificate has none */
    char     issuer[256];
    char     serial[96];           /* hex, as Windows shows it */
    char     algorithm[32];        /* the digest: "sha256" */
    char     countersigner[256];   /* the timestamp's signer, "" without one */
    int64_t  signed_at;            /* the timestamp (Unix seconds), 0 without one */
    int64_t  valid_from, valid_to;
    int32_t  status;               /* rubraview_sig_status_t */
    uint32_t error;                /* Windows' code when not OK (0x800B0100 none, ...) */
} rubraview_props_sig_t;

/** The words of a signature's status, Windows' ("This digital signature is OK."), not translated. */
const char *rubraview_props_sig_status_words(int32_t status);

/* ---- RV-281 phase 2: Security ---- */

/* The basic permissions of the Security tab, as Windows groups a file's access mask (FILE_GENERIC_* and the rest). */
typedef enum rubraview_right {
    RUBRAVIEW_RIGHT_FULL = 0,
    RUBRAVIEW_RIGHT_MODIFY,
    RUBRAVIEW_RIGHT_READ_EXECUTE,
    RUBRAVIEW_RIGHT_LIST,          /* folders only: Read & execute, inherited by folders only */
    RUBRAVIEW_RIGHT_READ,
    RUBRAVIEW_RIGHT_WRITE,
    RUBRAVIEW_RIGHT_SPECIAL,       /* shown, never set from the tab: what the others do not cover */
    RUBRAVIEW_RIGHT_COUNT
} rubraview_right_t;

#define RUBRAVIEW_SPECIAL_COUNT 13

/* The ACE flags the model keeps (Windows' values). */
enum {
    RUBRAVIEW_ACE_OBJECT_INHERIT    = 0x01,
    RUBRAVIEW_ACE_CONTAINER_INHERIT = 0x02,
    RUBRAVIEW_ACE_NO_PROPAGATE      = 0x04,
    RUBRAVIEW_ACE_INHERIT_ONLY      = 0x08,
    RUBRAVIEW_ACE_INHERITED         = 0x10,
};

/** A user or group of the list, its explicit and inherited Allow and Deny masks merged as the tab shows them. */
typedef struct rubraview_props_principal {
    char     sid[192];             /* "S-1-5-32-544" */
    char     name[256];            /* "BUILTIN\Administrators", or the SID when it has no name */
    uint32_t allow, deny;          /* explicit */
    uint32_t allow_inherited, deny_inherited;
    uint32_t elsewhere;            /* ACEs that only pass on to what is inside (inherit-only): shown as Special */
    uint8_t  flags;                /* the explicit ACEs' inheritance flags (RUBRAVIEW_ACE_*), kept when written back */
    bool     changed;              /* its boxes were clicked (or it was added): its explicit ACEs are written anew */
} rubraview_props_principal_t;

/** The right's words (Windows': "Full control", ...); `folder` for List folder contents. */
const char *rubraview_props_right_name(int32_t right);

/** Whether the right shows for a file or a folder (List folder contents: folders). */
bool rubraview_props_right_shown(int32_t right, bool folder);

/** The bits a basic right stands for (0 for Special). */
uint32_t rubraview_props_right_mask(int32_t right);

/** Whether `mask` holds the right (Special: bits no shown basic right covers, beyond SYNCHRONIZE). */
bool rubraview_props_right_held(uint32_t mask, int32_t right, bool folder);

/**
 * A box clicked, Windows' way: turned on, the right's bits are added (Full control turns every one on); turned off,
 * its bits and those of each right holding it go, but those of each smaller right still ticked stay (Modify off
 * under Full control leaves Read & execute and Write, nothing special).
 */
uint32_t rubraview_props_right_set(uint32_t mask, int32_t right, bool on, bool folder);

/** The Advanced view's thirteen rights: their bit and their words (a file's or a folder's). */
uint32_t rubraview_props_special_bit(int32_t i);
const char *rubraview_props_special_name(int32_t i, bool folder);

/**
 * The list's principals from a security descriptor's ACEs, in Windows' order (as read): each ACE once,
 * `allow`, `mask`, `flags` as Windows has them; merged by SID. Returns the count (at most `cap`).
 */
typedef struct rubraview_props_ace {
    char     sid[192];
    char     name[256];
    bool     allow;
    uint32_t mask;
    uint8_t  flags;
    uint8_t  audit;                /* an audit entry (SACL): 1 success, 2 failure, 3 both; 0 a permission */
} rubraview_props_ace_t;

size_t rubraview_props_principals_from(const rubraview_props_ace_t *aces, size_t count, rubraview_props_principal_t *out, size_t cap);

/**
 * The explicit ACEs to write back, in canonical order (every Deny before every Allow). A principal not changed keeps
 * its explicit ACEs from `original` as they were; a changed one gets one Deny and one Allow for what it has (its
 * flags, or this folder, subfolders and files for a new one on a folder), and keeps its inherit-only ACEs; a principal
 * no longer in the list loses its explicit ones. Inherited ACEs are never written (Windows adds them).
 * Returns the count (at most `cap`).
 */
size_t rubraview_props_explicit_aces(const rubraview_props_ace_t *original, size_t original_count,
                                     const rubraview_props_principal_t *list, size_t count, bool folder,
                                     rubraview_props_ace_t *out, size_t cap);

/*
 * What the elevated child is asked to write, and what it answers (`--shell-menu-host sec-write|audit-read|audit-write`),
 * one entry a line, fields split by tabs: "O<TAB>sid" a new owner, "P<TAB>1" inheritance off ("0" on),
 * "A<TAB>a|d<TAB>sid<TAB>mask<TAB>flags<TAB>name" a permission, "U<TAB>s|f|b<TAB>sid<TAB>mask<TAB>flags<TAB>name" an
 * audit entry, "E<TAB>words" an error; masks and flags in hex.
 */
typedef struct rubraview_props_sec {
    char owner_sid[192];           /* "" keeps the owner */
    int32_t protect;               /* -1 unchanged, 0 inherit, 1 inheritance off */
    bool write_dacl;               /* "D<TAB>1": the permissions given are written (else only the owner) */
    char error[256];
} rubraview_props_sec_t;

/** The lines for `sec` and `aces` into `out` (NUL-terminated); false when they do not fit. */
bool rubraview_props_sec_lines(const rubraview_props_sec_t *sec, const rubraview_props_ace_t *aces, size_t count, char *out, size_t cap);

/** The lines read back: `sec` filled, the entries into `aces`. Returns how many entries (at most `cap`). */
size_t rubraview_props_sec_read(proven_u8str_view_t text, rubraview_props_sec_t *sec, rubraview_props_ace_t *aces, size_t cap);

/** The bits Windows' generic rights in an ACE stand for on a file (GENERIC_READ ...), mapped. */
uint32_t rubraview_props_map_generic(uint32_t mask);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_FILEPROPS_H */
