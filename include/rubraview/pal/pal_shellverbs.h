#ifndef RUBRAVIEW_PAL_SHELLVERBS_H
#define RUBRAVIEW_PAL_SHELLVERBS_H

#include "rubraview/core.h"
#include "rubraview/shellverbs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RV-279: Explorer's menu inside Rubraview. The registry's verbs are read here, in the viewer, and nothing is loaded
 * for them. Explorer's own menus (its shell extensions, Send to, Open with, Windows 11's package items) are shown by
 * a child process, `rubraview.exe --shell-menu-host`, so a third party's code that crashes or hangs takes the child
 * with it, never the viewer.
 */

/** The verbs of `path` (UTF-8), as rubraview_shellverbs_list reads them, from Windows' registry. */
size_t rubraview_pal_shellverbs_list(const char *path, bool is_folder, bool shift, size_t files,
                                     rubraview_shellverb_t *out, size_t cap);

/**
 * Run a verb on each of `paths`: one with a command line in a process of its own (the file's folder its working
 * folder); one Windows runs (`by_shell`) in the child. False when one could not be started.
 */
bool rubraview_pal_shellverbs_run(const rubraview_shellverb_t *verb, const char *const *paths, size_t count);

typedef enum rubraview_shellmenu_kind {
    RUBRAVIEW_SHELLMENU_CLASSIC = 0,   /* Explorer's classic menu ("Show more options") */
    RUBRAVIEW_SHELLMENU_CLASSIC_EXTENDED, /* the same as Shift shows it */
    RUBRAVIEW_SHELLMENU_WIN11,         /* Windows 11's: its packages' items, then the classic menu's */
} rubraview_shellmenu_kind_t;

/**
 * Explorer's own menu of `paths` (one folder's files) at (x, y) in `native_window`'s client area, shown by the
 * child. Started, not waited for. False when the child could not be started.
 */
bool rubraview_pal_shellmenu_show(rubraview_shellmenu_kind_t kind, const char *const *paths, size_t count,
                                  void *native_window, double x, double y);

/** Whether Shift is down with the input being handled now (a right click with it opens the classic menu at once). */
bool rubraview_pal_shellmenu_shift_held(void);

/** The child's side: `--shell-menu-host <kind> <list file> <x> <y>`. Its exit code. */
int rubraview_pal_shellmenu_host_main(const wchar_t *kind, const wchar_t *list_file, const wchar_t *x, const wchar_t *y);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_SHELLVERBS_H */
