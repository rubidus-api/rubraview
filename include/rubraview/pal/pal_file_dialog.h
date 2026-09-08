#ifndef RUBRAVIEW_PAL_FILE_DIALOG_H
#define RUBRAVIEW_PAL_FILE_DIALOG_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "proven.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rv_file_filter {
    const char *description;  /* e.g. "Image Files (*.jpg;*.png;*.webp)" */
    const char *pattern;      /* e.g. "*.jpg;*.png;*.webp;*.gif" */
} rv_file_filter_t;

typedef struct rv_file_dialog_opts {
    const char             *title;
    const char             *default_dir;
    const rv_file_filter_t *filters;
    size_t                  filter_count;
    bool                    allow_multi;
    bool                    folder_mode;
    void                   *parent_window_handle; /* HWND on Win32, NSWindow* on macOS, wl_surface* on Wayland */
} rv_file_dialog_opts_t;

typedef struct rv_file_dialog_result {
    u8str_t *paths;          /* Array of selected UTF-8 file paths allocated in arena */
    size_t   count;          /* Number of paths selected */
    bool     accepted;       /* True if user confirmed selection, false if cancelled */
} rv_file_dialog_result_t;

/**
 * Open file selection dialog.
 * Allocates returned u8str_t array in the provided memory arena.
 */
rv_file_dialog_result_t rv_pal_file_dialog_open(proven_arena_t *arena, const rv_file_dialog_opts_t *opts);

/**
 * Save file selection dialog.
 */
rv_file_dialog_result_t rv_pal_file_dialog_save(proven_arena_t *arena, const rv_file_dialog_opts_t *opts);

/**
 * Folder / Directory picker dialog.
 */
rv_file_dialog_result_t rv_pal_file_dialog_pick_folder(proven_arena_t *arena, const rv_file_dialog_opts_t *opts);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_PAL_FILE_DIALOG_H */
