#ifndef RUBRAVIEW_FILEMANAGE_H
#define RUBRAVIEW_FILEMANAGE_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * File management, curation and lifecycle decisions (RFC-0001 §3.18,
 * §3.19), RV-070 to RV-074.
 *
 * These are the parts of M7 that decide something rather than call
 * something: what an undo would put back, whether a filename is legal,
 * which folder a number key means, what a drop should be treated as,
 * and whether a second launch should hand over to the first. The Windows
 * calls behind them — the recycle bin, `MoveFileExW`, a named mutex,
 * `WM_COPYDATA` — are in the PAL.
 *
 * The reason to draw the line here is that these are the decisions that
 * lose a reader's files when they are wrong.
 */

/* ---- §3.18.2 renaming ---- */

typedef enum rubraview_rename_err {
    RUBRAVIEW_RENAME_OK = 0,
    RUBRAVIEW_RENAME_ERR_EMPTY,
    RUBRAVIEW_RENAME_ERR_ILLEGAL_CHAR,   /* one of \ / : * ? " < > | */
    RUBRAVIEW_RENAME_ERR_RESERVED_NAME,  /* CON, PRN, AUX, NUL, COM1..9, LPT1..9 */
    RUBRAVIEW_RENAME_ERR_TRAILING,       /* Windows silently strips a trailing dot or space */
    RUBRAVIEW_RENAME_ERR_TOO_LONG,
} rubraview_rename_err_t;

/**
 * Whether a proposed *filename* (not a path) can be written on Windows.
 * The reserved device names are the part people forget: a file called
 * `NUL.jpg` cannot exist, and the rename would fail after the old name
 * had already gone.
 */
rubraview_rename_err_t rubraview_rename_validate(u8str_t filename);

/** A short explanation, for the rename box. */
u8str_t rubraview_rename_error_text(rubraview_rename_err_t err);

/**
 * §3.18.2: the rename box opens with the stem selected and the extension
 * left alone, so typing immediately replaces the title without losing
 * the format. Returns the stem's length in bytes.
 */
size_t rubraview_rename_stem_length(u8str_t filename);

/** Join a new stem to the old extension. */
u8str_t rubraview_rename_compose(proven_arena_t *arena, u8str_t old_filename, u8str_t new_stem);

/* ---- §3.18.1 / §3.18.3 the undo stack ---- */

typedef enum rubraview_file_op {
    RUBRAVIEW_FILE_OP_RECYCLE = 0,  /* to the recycle bin: undoable */
    RUBRAVIEW_FILE_OP_MOVE,         /* curation move: undoable */
    RUBRAVIEW_FILE_OP_COPY,         /* curation copy: the undo is a delete of the copy */
    RUBRAVIEW_FILE_OP_RENAME,       /* undoable by renaming back */
    RUBRAVIEW_FILE_OP_PURGE,        /* Shift+Delete: NOT undoable, and recorded as such */
} rubraview_file_op_t;

typedef struct rubraview_file_action {
    rubraview_file_op_t op;
    u8str_t source_path;      /* where it was */
    u8str_t target_path;      /* where it went; empty for a recycle or a purge */
    size_t  playlist_index;   /* where it sat in the sequence, so undo can put it back there */
} rubraview_file_action_t;

#define RUBRAVIEW_UNDO_CAPACITY 64

typedef struct rubraview_undo_stack {
    rubraview_file_action_t actions[RUBRAVIEW_UNDO_CAPACITY];
    size_t count;
} rubraview_undo_stack_t;

/**
 * Record an action. A purge is recorded too — not because it can be
 * undone, but so that `Ctrl+Z` after one can say *why* it will not work
 * instead of silently undoing the action before it, which would be the
 * worst possible answer.
 */
void rubraview_undo_push(rubraview_undo_stack_t *stack, rubraview_file_action_t action);

typedef enum rubraview_undo_result {
    RUBRAVIEW_UNDO_NOTHING = 0,      /* the stack is empty */
    RUBRAVIEW_UNDO_AVAILABLE,        /* `out_action` says what to reverse */
    RUBRAVIEW_UNDO_IRREVERSIBLE,     /* the last action was a permanent delete */
} rubraview_undo_result_t;

/**
 * What `Ctrl+Z` should do. The action is only removed from the stack
 * when the caller says the reversal succeeded, so a failed undo does not
 * quietly skip to the one before it.
 */
rubraview_undo_result_t rubraview_undo_peek(const rubraview_undo_stack_t *stack,
                                            rubraview_file_action_t *out_action);
void rubraview_undo_commit(rubraview_undo_stack_t *stack);

/** Whether an operation is one an undo can reverse at all. */
bool rubraview_file_op_is_undoable(rubraview_file_op_t op);

/* ---- §3.18.3 curation ---- */

typedef enum rubraview_curation_mode {
    RUBRAVIEW_CURATION_MOVE = 0,
    RUBRAVIEW_CURATION_COPY,
} rubraview_curation_mode_t;

typedef struct rubraview_curation {
    u8str_t dirs[9];                  /* dir_1 .. dir_9; an empty slot is unbound */
    rubraview_curation_mode_t mode;
} rubraview_curation_t;

/** Read the `[curation]` section of settings.ini (§3.18.3). */
rubraview_curation_t rubraview_curation_parse(proven_arena_t *arena, u8str_t settings_ini);

/**
 * The folder a number key means, or an empty slice when that key is not
 * bound. `digit` is 1 to 9.
 */
u8str_t rubraview_curation_target(const rubraview_curation_t *curation, int32_t digit);

/**
 * What the viewer should do after a curation action: a move takes the
 * file out of the sequence, so the viewer advances; a copy leaves it
 * where it is, so the viewer stays. Getting this backwards would make
 * copy mode unusable.
 */
bool rubraview_curation_advances(const rubraview_curation_t *curation);

/* ---- §3.19.2 drops ---- */

typedef enum rubraview_drop_kind {
    RUBRAVIEW_DROP_NOTHING = 0,
    RUBRAVIEW_DROP_OPEN_FILE,     /* one file: open it */
    RUBRAVIEW_DROP_OPEN_FOLDER,   /* one directory: open it as a gallery */
    RUBRAVIEW_DROP_PLAYLIST,      /* several: a temporary playlist of exactly those */
} rubraview_drop_kind_t;

typedef struct rubraview_drop_item {
    u8str_t path;
    bool    is_directory;
} rubraview_drop_item_t;

/**
 * §3.19.2's routing rule, decided from the payload alone. A drop of one
 * folder plus one file is several things, so it becomes a playlist —
 * anything else would have to guess which one the reader meant.
 */
rubraview_drop_kind_t rubraview_drop_classify(const rubraview_drop_item_t *items, size_t count);

/* ---- §3.19.1 single instance ---- */

typedef enum rubraview_instance_action {
    RUBRAVIEW_INSTANCE_RUN = 0,     /* no other instance, or single-instance is off */
    RUBRAVIEW_INSTANCE_HAND_OVER,   /* send the path to the running one and exit 0 */
} rubraview_instance_action_t;

/**
 * §3.19.1. Handing over is only right when there is something to hand:
 * launching a second copy with no arguments while one is already running
 * should still bring that one forward, but a launch with no arguments
 * and no running instance must start normally.
 */
rubraview_instance_action_t rubraview_instance_decide(bool single_instance_enabled,
                                                      bool another_is_running);

/* ---- §3.19.3 shell registration ---- */

/**
 * The ProgID for one extension, e.g. "jpg" → "Rubraview.jpg". Written
 * once here so registering and unregistering cannot disagree about the
 * name — which is how leftover registry entries happen.
 */
u8str_t rubraview_shell_progid(proven_arena_t *arena, u8str_t extension);

/** The extensions §3.19.3 offers to associate, as a `;`-separated list. */
u8str_t rubraview_shell_extensions(void);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_FILEMANAGE_H */
