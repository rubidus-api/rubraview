#ifndef RUBRAVIEW_FILEOP_H
#define RUBRAVIEW_FILEOP_H

#include "rubraview/core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-258 (owner, 2026-11-08: "압축파일 내에서 오건 어디서 오건 파일 관리자에서 동일한 이름의 파일이 있고 그걸 덮어씌우려 할 때,
 * 그 파일은 일단 킵해놓고 일괄적으로 묻게 해 줘요. 그러니까 아예 처음에 한꺼번에 이름을 확인하는 절차를 제일 먼저 하고, 안겹치는
 * 파일들을 복사 또는 이동하면서, 모달레스로 겹치는 파일 어떻게 할지 작업 큐에 쌓아가면서 사용자에게 묻고 처리하게 해요."; the
 * design read from rubraman's, D-310): a copy or a move into a folder as an operation of items.
 *
 * rubraview_fileop_prepare checks every name first. A folder whose name is taken is merged: its entries become items
 * of their own, parents first. An item whose name is taken is parked with its question, and nothing at the
 * destination is touched until it is answered. rubraview_fileop_step carries out the pending items, a few at a time,
 * while the parked ones wait. An answer puts its item back among the pending ones.
 *
 * A file lands under a temporary name beside its destination and is renamed into place only when whole. An
 * overwrite swaps the staged file in, and the replaced one goes to the Recycle Bin (the fs's `replace`). A move to
 * another volume is a copy, its bytes compared and flushed, and then the source goes to the Recycle Bin. A file is
 * never copied onto itself. Links are never followed.
 *
 * Everything the file system does goes through rubraview_fileop_fs_t, so the rules are tested with a fake one. The
 * operation is one thread's: the app steps it on a worker and takes the main thread's answers under a lock of its
 * own.
 */

typedef enum { RUBRAVIEW_FILEOP_COPY = 0, RUBRAVIEW_FILEOP_MOVE = 1 } rubraview_fileop_kind_t;

typedef enum {
    RUBRAVIEW_FILEOP_PENDING = 0,
    RUBRAVIEW_FILEOP_PARKED,      /* its name is taken: waiting for an answer */
    RUBRAVIEW_FILEOP_DONE,
    RUBRAVIEW_FILEOP_SKIPPED,
    RUBRAVIEW_FILEOP_FAILED,
    RUBRAVIEW_FILEOP_MERGED,      /* a folder that was there already: its entries are items of their own */
} rubraview_fileop_state_t;

typedef enum {
    RUBRAVIEW_FILEOP_ANSWER_NONE = 0,
    RUBRAVIEW_FILEOP_ANSWER_SKIP,        /* the one there kept, the one coming left where it is */
    RUBRAVIEW_FILEOP_ANSWER_OVERWRITE,   /* the one coming in its place; the one there to the Recycle Bin */
    RUBRAVIEW_FILEOP_ANSWER_KEEP_BOTH,   /* the one coming as "name (2).ext", (3) ... */
    RUBRAVIEW_FILEOP_ANSWER_SAME_ONLY,   /* compared: the same — the one there kept (see same_recycles_copy);
                                            different — both kept, the one coming with a suffix */
} rubraview_fileop_answer_t;

/* What an item ended as, for the undo and the summary. */
typedef enum {
    RUBRAVIEW_FILEOP_DID_NOTHING = 0,
    RUBRAVIEW_FILEOP_DID_COPY,
    RUBRAVIEW_FILEOP_DID_MOVE,
    RUBRAVIEW_FILEOP_DID_MAKE_DIR,
    RUBRAVIEW_FILEOP_DID_RECYCLE_SOURCE,   /* the same file was there: the source went to the Recycle Bin */
} rubraview_fileop_did_t;

typedef struct rubraview_fileop_stat {
    bool     exists, is_dir, is_link;
    uint64_t size;
    int64_t  mtime;             /* seconds */
    uint64_t volume, file_id;   /* its identity (0, 0 when unknown) */
} rubraview_fileop_stat_t;

/* The file system, as the operation needs it; paths are UTF-8 and NUL-ended. */
typedef struct rubraview_fileop_fs {
    void *ctx;
    bool (*stat)(void *ctx, const char *path, rubraview_fileop_stat_t *out);   /* false: nothing there */
    /* The names in `dir` (not "." or ".."), each given to `each`; false when it could not be listed. */
    bool (*list)(void *ctx, const char *dir, void (*each)(void *user, const char *name), void *user);
    bool (*copy)(void *ctx, const char *from, const char *to);          /* `to` must not be there */
    bool (*rename)(void *ctx, const char *from, const char *to);        /* never over a name; false across volumes */
    bool (*replace)(void *ctx, const char *target, const char *staged); /* staged in target's place, target recycled */
    bool (*recycle)(void *ctx, const char *path);
    bool (*remove)(void *ctx, const char *path);                        /* a staging file of ours, gone for good */
    bool (*make_dir)(void *ctx, const char *path);
    bool (*remove_empty_dir)(void *ctx, const char *path);
    int  (*same_bytes)(void *ctx, const char *a, const char *b);        /* 1 the same, 0 not, -1 could not tell */
    bool (*flush)(void *ctx, const char *path);
    bool (*same_volume)(void *ctx, const char *a, const char *b);
} rubraview_fileop_fs_t;

typedef struct rubraview_fileop_item {
    char *source, *target;      /* target: where it goes (its final name once kept beside another) */
    bool  is_dir, merged_child; /* merged_child: under a folder that was merged */
    uint64_t size;
    rubraview_fileop_state_t  state;
    rubraview_fileop_answer_t answer;
    rubraview_fileop_did_t    did;
    bool  overwrote;            /* it replaced a file (that one is in the Recycle Bin: the undo is partial) */
    bool  kind_differs;         /* a file where a folder is, or the reverse: only skip or keep both */
    bool  expanded;             /* a folder made at the destination, its entries items of their own */
    bool  prepared;
    int32_t parent;             /* the merged folder it came from, or -1 */
    const char *why;            /* a failure's reason, a static English text */
} rubraview_fileop_item_t;

typedef struct rubraview_fileop {
    rubraview_fileop_kind_t kind;
    rubraview_fileop_fs_t fs;
    char *dir;                          /* the destination folder */
    rubraview_fileop_item_t *items;
    size_t count, capacity, cursor;
    bool same_recycles_copy;            /* a copy's same file: its source to the Recycle Bin (the owner's choice), or left */
    uint32_t serial;                    /* for the staging names */
    size_t done, skipped, failed, parked;
} rubraview_fileop_t;

void rubraview_fileop_init(rubraview_fileop_t *op, rubraview_fileop_kind_t kind, const rubraview_fileop_fs_t *fs, const char *dir);
void rubraview_fileop_free(rubraview_fileop_t *op);

/* A file or folder to copy or move into the folder. False when the memory is not there. */
bool rubraview_fileop_add(rubraview_fileop_t *op, const char *source);

/* Every name checked before anything is done: the clear items pending, the taken ones parked, folders that are
   there already merged into their entries, a folder into itself and a file onto itself (a move) skipped, a copy onto
   itself kept beside it. How many are parked. */
size_t rubraview_fileop_prepare(rubraview_fileop_t *op);

/* Up to `max` pending items carried out. True while pending ones are left (parked ones wait for answers). */
bool rubraview_fileop_step(rubraview_fileop_t *op, size_t max);

/* A parked item answered (and put back among the pending ones); false when it is not parked or the answer does
   not fit it (overwrite or same-only where a file and a folder meet). */
bool rubraview_fileop_answer(rubraview_fileop_t *op, size_t index, rubraview_fileop_answer_t answer);

/* Every parked item answered the same; how many were. */
size_t rubraview_fileop_answer_all(rubraview_fileop_t *op, rubraview_fileop_answer_t answer);

/* RV-258: the owner's check boxes, as Explorer's — keep the one coming, keep the one there: both kept (a suffix),
   the one coming alone (overwrite), the one there alone or neither (skip). */
rubraview_fileop_answer_t rubraview_fileop_answer_of_checks(bool keep_coming, bool keep_there);

/* RV-269 (owner, 2026-11-10: "복사 이동도 용량 기준으로도 표시해 주세요"): the bytes of the files it carries (folders
   not counted, only items checked by prepare), and those of the ones ended — done, skipped or failed. A file being
   copied is not in `done` until it ends: the copy's own progress is the caller's (the fs `copy`'s `ctx`). */
void rubraview_fileop_bytes(const rubraview_fileop_t *op, uint64_t *done, uint64_t *total);

/* RV-259 (owner, 2026-11-08: "윈도 스타일로 (2) (3)"): `path` with " (n)" before its extension (a folder: at its end), into `out`. */
bool rubraview_fileop_suffix_name(const char *path, bool is_dir, unsigned n, char *out, size_t cap);

/* Nothing pending and nothing parked. */
bool rubraview_fileop_finished(const rubraview_fileop_t *op);

#ifdef __cplusplus
}
#endif

#endif
