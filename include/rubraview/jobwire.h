#ifndef RUBRAVIEW_JOBWIRE_H
#define RUBRAVIEW_JOBWIRE_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-289 (owner, 2026-11-15: "이것들은 어떤 독립된 프로세스로 따로 처리하는 것이 바람직하지 않을까요? 메인 창이 죽거나 해도
 * 그대로 이어서 진행되고, 반대로 이 프로세스가 죽어도 메인 창이 죽지 않게요"; "작업마다 프로세스"): what the viewer and a
 * file job's process say to each other over the job's pipe. A message is a u32 length (of what follows it), a type
 * byte, then its fields: u8, u32, u64, i32, i64 little-endian, and UTF-8 strings as a u32 length and the bytes. A
 * message longer than RUBRAVIEW_JOBWIRE_MAX, or one whose fields run past its end, is refused. Host-tested.
 */

#define RUBRAVIEW_JOBWIRE_MAX (64u * 1024u * 1024u)

typedef enum rubraview_jobwire_type {
    /* the viewer to the job */
    RUBRAVIEW_JOBWIRE_START = 1,      /* the job: rubraview_jobwire_start_t, then its sources */
    RUBRAVIEW_JOBWIRE_STOP = 2,       /* stop after the file in hand */
    RUBRAVIEW_JOBWIRE_SHOW_CLASHES = 3,
    RUBRAVIEW_JOBWIRE_ROWS = 4,       /* an extraction's or an archive's clashes: a u32 count, then rows */
    RUBRAVIEW_JOBWIRE_ARCHIVE = 5,    /* RV-292: an extraction or a compression: rubraview_jobwire_archive_t */
    /* the job to the viewer */
    RUBRAVIEW_JOBWIRE_PROGRESS = 16,
    RUBRAVIEW_JOBWIRE_ITEMS = 17,     /* a u32 count, then items */
    RUBRAVIEW_JOBWIRE_END = 18,
    RUBRAVIEW_JOBWIRE_ANSWER = 19,    /* the answer to a row of ROWS */
    RUBRAVIEW_JOBWIRE_ARCHIVE_DONE = 20,   /* RV-292: how an extraction or a compression went: rubraview_jobwire_archive_done_t */
} rubraview_jobwire_type_t;

/* ---- writing ---- */

typedef struct rubraview_jobwire_out {
    uint8_t *data;
    size_t   len, cap;
    size_t   start;    /* where the message being written begins */
    bool     failed;   /* out of memory, or a message too long: the message is not finished */
} rubraview_jobwire_out_t;

void rubraview_jobwire_begin(rubraview_jobwire_out_t *out, uint8_t type);
void rubraview_jobwire_put_u8(rubraview_jobwire_out_t *out, uint8_t v);
void rubraview_jobwire_put_u32(rubraview_jobwire_out_t *out, uint32_t v);
void rubraview_jobwire_put_u64(rubraview_jobwire_out_t *out, uint64_t v);
void rubraview_jobwire_put_i32(rubraview_jobwire_out_t *out, int32_t v);
void rubraview_jobwire_put_i64(rubraview_jobwire_out_t *out, int64_t v);
void rubraview_jobwire_put_str(rubraview_jobwire_out_t *out, proven_u8str_view_t s);
/** The message's length written in; false (and the message taken back) when it failed or is too long. */
bool rubraview_jobwire_finish(rubraview_jobwire_out_t *out);
/** The first `n` bytes sent: the rest moves to the front. */
void rubraview_jobwire_out_drop(rubraview_jobwire_out_t *out, size_t n);
void rubraview_jobwire_out_free(rubraview_jobwire_out_t *out);

/* ---- reading ---- */

typedef struct rubraview_jobwire_msg {
    uint8_t        type;
    const uint8_t *p;      /* its fields not yet read */
    size_t         left;
    bool           bad;    /* a field ran past the end */
} rubraview_jobwire_msg_t;

/**
 * The first message of `data`: 1 and `*used` its bytes when it is all there; 0 when more must come; -1 when it
 * can never be one (a length of nought or over the most). The message's strings point into `data`.
 */
int rubraview_jobwire_take(const uint8_t *data, size_t len, rubraview_jobwire_msg_t *msg, size_t *used);
uint8_t  rubraview_jobwire_get_u8(rubraview_jobwire_msg_t *msg);
uint32_t rubraview_jobwire_get_u32(rubraview_jobwire_msg_t *msg);
uint64_t rubraview_jobwire_get_u64(rubraview_jobwire_msg_t *msg);
int32_t  rubraview_jobwire_get_i32(rubraview_jobwire_msg_t *msg);
int64_t  rubraview_jobwire_get_i64(rubraview_jobwire_msg_t *msg);
proven_u8str_view_t rubraview_jobwire_get_str(rubraview_jobwire_msg_t *msg);
/** Every field read and none ran past the end. */
bool rubraview_jobwire_done(const rubraview_jobwire_msg_t *msg);

/* ---- the messages ---- */

typedef struct rubraview_jobwire_start {
    bool     moving;
    proven_u8str_view_t dir;        /* the folder they go into */
    int32_t  basket;                /* the basket it empties as it moves, or -1 */
    bool     from_cut;
    int32_t  viewer_page;           /* the viewer's own Copy / Move to folder of the page on screen, or -1 */
    proven_u8str_view_t viewer_path;
    proven_u8str_view_t temp_dir;   /* removed by the job at its end ("" none) */
    bool     same_recycles_copy;
    proven_u8str_view_t lang_file;  /* the language pack the viewer reads ("" English), for the job's windows */
    proven_u8str_view_t ui_font;    /* Settings › General › the windows' face, as the setting holds it */
    proven_u8str_view_t journal_dir;/* RV-292: where the job leaves its note ("" none) */
    uint32_t count;                 /* the sources that follow */
} rubraview_jobwire_start_t;

/** START with its sources. */
void rubraview_jobwire_put_start(rubraview_jobwire_out_t *out, const rubraview_jobwire_start_t *start,
                                 const proven_u8str_view_t *sources, size_t count);
/** START's head; then `count` rubraview_jobwire_get_str for the sources. */
void rubraview_jobwire_get_start(rubraview_jobwire_msg_t *msg, rubraview_jobwire_start_t *start);

typedef struct rubraview_jobwire_progress {
    uint64_t total, done, failed, skipped;
    uint64_t bytes_total, bytes_done;
    uint64_t clashes;               /* names waiting for an answer */
} rubraview_jobwire_progress_t;

void rubraview_jobwire_put_progress(rubraview_jobwire_out_t *out, const rubraview_jobwire_progress_t *p);
void rubraview_jobwire_get_progress(rubraview_jobwire_msg_t *msg, rubraview_jobwire_progress_t *p);

/** One item of the job as `job_ended` reads it (rubraview_fileop_item_t's did, state, ...). */
typedef struct rubraview_jobwire_item {
    proven_u8str_view_t source, target, why;
    uint8_t did, state;
    bool    overwrote;
    int32_t parent;
} rubraview_jobwire_item_t;

void rubraview_jobwire_put_item(rubraview_jobwire_out_t *out, const rubraview_jobwire_item_t *item);
void rubraview_jobwire_get_item(rubraview_jobwire_msg_t *msg, rubraview_jobwire_item_t *item);

typedef struct rubraview_jobwire_end {
    bool     moving;
    uint64_t skipped, failed;
    bool     stopped;
} rubraview_jobwire_end_t;

void rubraview_jobwire_put_end(rubraview_jobwire_out_t *out, const rubraview_jobwire_end_t *end);
void rubraview_jobwire_get_end(rubraview_jobwire_msg_t *msg, rubraview_jobwire_end_t *end);

/** A clash found by an extraction or an archive's writing, asked in the job's window. */
typedef struct rubraview_jobwire_row {
    uint32_t owner;                  /* whose: the extraction's or the archive's */
    uint64_t index;
    proven_u8str_view_t source, target;
    uint64_t ssize, tsize;
    int64_t  smtime, tmtime;
} rubraview_jobwire_row_t;

void rubraview_jobwire_put_row(rubraview_jobwire_out_t *out, const rubraview_jobwire_row_t *row);
void rubraview_jobwire_get_row(rubraview_jobwire_msg_t *msg, rubraview_jobwire_row_t *row);

typedef struct rubraview_jobwire_answer {
    uint32_t owner;
    uint64_t index;
    bool     all;
    uint8_t  answer;                 /* rubraview_fileop_answer_t */
} rubraview_jobwire_answer_t;

void rubraview_jobwire_put_answer(rubraview_jobwire_out_t *out, const rubraview_jobwire_answer_t *a);
void rubraview_jobwire_get_answer(rubraview_jobwire_msg_t *msg, rubraview_jobwire_answer_t *a);

/* ---- RV-292: an extraction or a compression in a job's process ---- */

enum { RUBRAVIEW_JOBWIRE_EXTRACT = 1, RUBRAVIEW_JOBWIRE_COMPRESS = 2,
       RUBRAVIEW_JOBWIRE_ADD = 3   /* files into an archive: `dest` the archive, `output` the new one written beside it, the names
                                      (path, name in the archive) in pairs, `only` the entries they replace, `format` the target,
                                      `mode` 1 to make it a ZIP or 7z of its own name; the answer's `made` the archive, `set_name`
                                      the old one kept (name.rvold), `result` an arcedit result */ };

typedef struct rubraview_jobwire_archive {
    uint8_t  kind;                     /* RUBRAVIEW_JOBWIRE_EXTRACT or _COMPRESS */
    bool     quiet;                    /* no window of its own (Explorer's extraction window shows it) */
    proven_u8str_view_t journal_dir, lang_file, ui_font;
    uint32_t count;                    /* the archives (extract) or the sources (compress) that follow, as strings */
    /* extract */
    proven_u8str_view_t dest;          /* "" beside each */
    uint8_t  mode;                     /* rubraview_extract_mode_t */
    uint32_t only_count;               /* the entries picked, u64 each after the names; 0 all */
    proven_u8str_view_t inner;
    uint32_t rules_count;              /* a rule a picked entry (u8 each after them), or 0 */
    int32_t  codepage;
    /* compress */
    proven_u8str_view_t output;
    uint8_t  format, method;
    int32_t  level;
    bool     solid_off, names_hidden;
    uint64_t volume_bytes;
    /* both */
    proven_u8str_view_t password;      /* "" none; never written anywhere else */
} rubraview_jobwire_archive_t;

/** ARCHIVE with its names, then its picked entries and their rules. */
void rubraview_jobwire_put_archive(rubraview_jobwire_out_t *out, const rubraview_jobwire_archive_t *a, const proven_u8str_view_t *names,
                                   const size_t *only, const uint8_t *rules);
/** ARCHIVE's head; then `count` strings, `only_count` u64 and `rules_count` u8 read by the caller. */
void rubraview_jobwire_get_archive(rubraview_jobwire_msg_t *msg, rubraview_jobwire_archive_t *a);

typedef struct rubraview_jobwire_archive_done {
    uint8_t  result;                   /* rubraview_archive_result_t */
    uint32_t written, skipped, failed, at;   /* at: the archive it stopped at */
    proven_u8str_view_t root, why;
    proven_u8str_view_t made, set_name;      /* compress: the archive made; a split set's name */
    uint32_t files, folders;
    bool     older_parts;
    uint32_t parked;                   /* names taken: (u32 archive, root, u64 entry, u64 size, i64 mtime, path) each after */
} rubraview_jobwire_archive_done_t;

void rubraview_jobwire_begin_archive_done(rubraview_jobwire_out_t *out, const rubraview_jobwire_archive_done_t *d);
void rubraview_jobwire_get_archive_done(rubraview_jobwire_msg_t *msg, rubraview_jobwire_archive_done_t *d);

#ifdef __cplusplus
}
#endif

#endif
