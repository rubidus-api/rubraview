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

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_FILEPROPS_H */
