#ifndef RUBRAVIEW_PAL_FILE_DIALOG_H
#define RUBRAVIEW_PAL_FILE_DIALOG_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rubraview_file_filter {
    const char *description;  /* e.g. "Image Files (*.jpg;*.png;*.webp)" */
    const char *pattern;      /* e.g. "*.jpg;*.png;*.webp;*.gif" */
} rubraview_file_filter_t;

typedef struct rubraview_file_dialog_opts {
    const char             *title;
    const char             *default_dir;
    const rubraview_file_filter_t *filters;
    size_t                  filter_count;
    bool                    allow_multi;
    bool                    folder_mode;
    void                   *parent_window_handle; /* HWND on Win32, NSWindow* on macOS, wl_surface* on Wayland */
} rubraview_file_dialog_opts_t;

typedef struct rubraview_file_dialog_result {
    proven_u8str_view_t *paths;          /* Array of selected UTF-8 file paths allocated in arena */
    size_t   count;          /* Number of paths selected */
    bool     accepted;       /* True if user confirmed selection, false if cancelled */
} rubraview_file_dialog_result_t;

/**
 * Open file selection dialog.
 * Allocates returned proven_u8str_view_t array in the provided memory arena.
 */
rubraview_file_dialog_result_t rubraview_pal_file_dialog_open(proven_arena_t *arena, const rubraview_file_dialog_opts_t *opts);

/**
 * Save file selection dialog.
 */
rubraview_file_dialog_result_t rubraview_pal_file_dialog_save(proven_arena_t *arena, const rubraview_file_dialog_opts_t *opts);

/**
 * Folder / Directory picker dialog.
 */
rubraview_file_dialog_result_t rubraview_pal_file_dialog_pick_folder(proven_arena_t *arena, const rubraview_file_dialog_opts_t *opts);

/**
 * The owner (2026-10-17): Windows' own font dialog, `current` chosen in it.
 * The face name picked is written to `out` (UTF-8, NUL-terminated); false
 * when the dialog was cancelled or there is none on this platform.
 */
bool rubraview_pal_choose_font(void *parent_window_handle, proven_u8str_view_t current, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_FILE_DIALOG_H */
