/* RV-289: the window that asks about names already there, apart from the viewer's state — shown by a file job's
   process for its own clashes, and by the viewer for an extraction's and an archive's until they go to a job too.
   (It was filejob.c's, RV-258 / D-310: the same rows, ticks, buttons and words.) */
#ifndef RUBRAVIEW_CLASHUI_H
#define RUBRAVIEW_CLASHUI_H

#include "rubraview/fileop.h"
#include "rubraview/pal/pal_window.h"
#include "rubraview/pal/pal_render.h"
#include "rubraview/pal/pal_thumbs.h"
#include "rubraview/ui_scrollbar.h"

/* RV-289.7: a file's picture, kept while the list is open (by its path, size and time) */
typedef struct clashui_thumb {
    char    *path;
    uint64_t size;
    int64_t  mtime;
    int      state;               /* 0 not asked, 1 asked, 2 made, 3 none to be had */
    rubraview_texture_t *texture;
} clashui_thumb_t;

typedef struct clashui_row {
    uint32_t owner;           /* whose question: a job's id, or an extraction's / an archive's mark */
    size_t   index;           /* its item there */
    char    *source, *target;
    uint64_t ssize, tsize;
    int64_t  smtime, tmtime;
    bool     kind_differs;    /* a file and a folder: skip or keep both only */
    bool     keep_coming, keep_there;
} clashui_row_t;

typedef struct clashui clashui_t;
typedef void (*clashui_answer_fn)(void *ctx, const clashui_row_t *row, rubraview_fileop_answer_t answer);
typedef void (*clashui_all_fn)(void *ctx, rubraview_fileop_answer_t answer);

struct clashui {
    /* given */
    void              *ctx;
    clashui_answer_fn  answer;       /* a row's ticks applied */
    clashui_all_fn     answer_all;   /* a button for all */
    void             (*closed)(void *ctx);   /* closed by its X or Esc with questions left (RV-289: they are skipped) */
    void             (*recycle_set)(void *ctx, bool on);   /* the copy's same file: its source recycled or left */
    bool               same_recycles_copy;
    proven_u8str_view_t ui_font;     /* Settings › General's face ("" the default) */
    /* its own */
    clashui_row_t     *rows;
    size_t             nrows;
    int32_t            scroll;
    bool               open, dirty;
    rubraview_window_t   *window;
    rubraview_renderer_t *renderer;
    int32_t            frame[4];
    double             px, py;
    /* RV-289.6: the scroll bar, in rows */
    rubraview_scrollbar_t      sb;
    rubraview_scrollbar_hold_t hold;
    int32_t            rows_shown;
    /* RV-289.7: the thumbnails — those on screen asked first, then the rows near them */
    rubraview_thumbs_t *thumbs;
    clashui_thumb_t    *cache;
    size_t              ncache, cap;
};

/* Once a pass: what a held scroll bar does, and the thumbnails taken and asked. */
void clashui_tick(clashui_t *ui);

/** The rows now waiting (taken over; the ticks of a row already shown are kept). */
void clashui_set_rows(clashui_t *ui, clashui_row_t *rows, size_t n);
void clashui_rows_free(clashui_row_t *rows, size_t n);
/** Opened (when there are rows) on `arena`, which lives as long as the window. False when it could not be. */
bool clashui_show(clashui_t *ui, proven_arena_t *arena);
void clashui_close(clashui_t *ui);
/** Its messages, once a pass; the number handled. */
size_t clashui_pump(clashui_t *ui);
void clashui_draw(clashui_t *ui);

#endif
