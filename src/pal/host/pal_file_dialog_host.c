#include "rubraview/pal/pal_file_dialog.h"

rubraview_file_dialog_result_t rubraview_pal_file_dialog_open(proven_arena_t *arena, const rubraview_file_dialog_opts_t *opts) {
    (void)arena;
    (void)opts;
    /* Headless host mock: returns empty non-accepted result */
    return (rubraview_file_dialog_result_t){
        .paths = NULL,
        .count = 0,
        .accepted = false,
    };
}

rubraview_file_dialog_result_t rubraview_pal_file_dialog_save(proven_arena_t *arena, const rubraview_file_dialog_opts_t *opts) {
    (void)arena;
    (void)opts;
    return (rubraview_file_dialog_result_t){
        .paths = NULL,
        .count = 0,
        .accepted = false,
    };
}

rubraview_file_dialog_result_t rubraview_pal_file_dialog_pick_folder(proven_arena_t *arena, const rubraview_file_dialog_opts_t *opts) {
    (void)arena;
    (void)opts;
    return (rubraview_file_dialog_result_t){
        .paths = NULL,
        .count = 0,
        .accepted = false,
    };
}

bool rubraview_pal_choose_font(void *parent_window_handle, u8str_t current, char *out, size_t cap) {
    (void)parent_window_handle;
    (void)current;
    if (out && cap) out[0] = '\0';
    return false;
}
